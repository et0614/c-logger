/**
 * @file main.c
 * @brief AVR(AVR64DU32)を使用した温湿度・CO2計測ロギングプログラム
 * @author E.Togashi
 * @date 2026/6/20
 *
 * 動作モード:
 *   USB通信モード (起動時の既定, isLogging=false):
 *     USBをポーリングしてPCコマンドを処理。LEDは省電力のため1秒ごとに2回点滅(ハートビート)。
 *   ロギングモード (start_loggingで移行, isLogging=true):
 *     STANDBYスリープ + PIT(1秒)で定期計測。省電力のためLEDは消灯のまま(点滅しない)。
 *     USBは処理しない。
 *   共通: RSTボタンを押している間はLED点灯(計測できているか/生存の確認)。5秒以上の
 *         長押しでソフトウェアリセット → USB通信モードに戻る。
 */

// <editor-fold defaultstate="collapsed" desc="ヘッダインクルード">

//自動生成ヘッダ
#include "mcc_generated_files/system/clock.h" //F_CPUの設定
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc0.h"     // ADC（電池電圧計測）
#include "mcc_generated_files/timer/delay.h"

//自作ヘッダ
#include "main.h"
#include "hal_io.h"
#include "adc0_extension.h" //AD変換拡張 (ADC基準電圧設定)
#include "eeprom_manager.h" //EEPROM
#include "usb_comm.h"       //USB-CDC通信 (PCコマンド)
#include "measurement.h"    //温湿度・CO2計測

//標準ヘッダ
#include <avr/io.h>        // RTC / RSTCTRL / PORT レジスタ, _PROTECTED_WRITE
#include <avr/wdt.h>       // _PROTECTED_WRITE 提供のため
#include <avr/sleep.h>     // set_sleep_mode / sleep_mode
#include <avr/interrupt.h> // sei / cli

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="広域変数定義">

//1秒毎の処理実行フラグ (PIT割り込みで立つ)
volatile bool process_logging_flag = false;

//電池不足継続タイマ[sec]
static uint8_t lowBattery_timer = 0;

//リセットボタン押し込み継続タイマ[sec]
static uint8_t reset_timer = 0;

//計測(ロギング)モードか否か。false = USB通信モード(既定)。
static bool isLogging = false;

//ソフトウェア時計 (UNIX epoch秒)。set_timeコマンドで設定し、PIT(1秒)毎にインクリメント。
//各レコードの時刻は start_dt + index*interval_co2 で復元するため start_dt の基準に使う。
static uint32_t g_epoch = 0;

//コンディショニング(長期未通電復帰用)の残り待ち秒数。start_logging時にセットし、PIT毎に
//デクリメント。0になるまで計測を始めない(その間はPITスリープで時間を消化する)。
static uint8_t conditioning_wait = 0;

//start_dt(1レコード目の時刻基準)を確定済みか。コンディショニング完了後の初回計測で確定する。
static bool start_dt_set = false;

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="main">

int main(void)
{
    SYSTEM_Initialize();   // クロック / ピン(LED,RST) / ADC / I2C / SPI / USB / RTC 等

    // SPIモジュールを有効化 (内蔵フラッシュ W25Q64 用)
    SPI0_Open(SPI0_DEFAULT);

    //EEPROMロード (計測設定・start_dt 等)
    EM_loadEEPROM();

    // 計測の初期化(STCC4のI2C初期化 + rec_latest復元)は boot時には行わない。
    // boot時にI2Cを叩くと、STCC4無応答時に I2C 完了待ちでハングしUSB初期化に到達できないため、
    // start_logging受信時(setLoggingActive)に初期化する。

    //低電圧が1秒継続したら電池残量エラー(起動時ガード)
    int count = 0;
    while(isLowBattery()){
        count++;
        if(10 <= count) showError(1);
        DELAY_milliseconds(100);
    }

    // PIT(1秒)割り込みコールバック登録
    RTC_SetPITIsrCallback(oneSecHandler);

    // RSTボタン(PA0)を両エッジ割り込みに設定し、押下/解放へ即時反応させる(LED点灯/消灯)。
    // プルアップ(PULLUPEN)等のMCC設定は保持し、ISC(割り込みセンス)のみ両エッジに上書きする。
    PORTA.PIN0CTRL = (PORTA.PIN0CTRL & ~PORT_ISC_gm) | PORT_ISC_BOTHEDGES_gc;
    RST_SetInterruptHandler(rstButtonHandler);

    // ロギング中のSTANDBYスリープでもPITで毎秒起床できるよう RUNSTDBY を有効化。
    // (MCC生成 RTC_Initialize は RUNSTDBY 無効で初期化している)
    while (RTC.STATUS > 0) ;
    RTC.CTRLA |= RTC_RUNSTDBY_bm;

    DELAY_milliseconds(100);
    sei();
    DELAY_milliseconds(100);

    // USB仮想シリアル通信の初期化 (CDC層。USBデバイス層はSYSTEM_Initializeで初期化済み)
    USB_Comm_Initialize();

    // 10秒以上電圧不足が継続したら終了 (フラッシュ書込み中の電池切れによる破損を防ぐ)
    while (lowBattery_timer <= 10)
    {
        //1秒毎の処理 (PITで process_logging_flag が立つ)
        if(process_logging_flag)
        {
            process_logging_flag = false;
            executeSecondlyTask();
        }

        if(isLogging)
        {
            // ロギングモード: STANDBYスリープし、PIT(1秒)で起床して計測する。
            // USBは処理しない(省電力。CR2032でのUSBアクティブ連続稼働は不可のため)。
            set_sleep_mode(SLEEP_MODE_STANDBY);
            sleep_mode();
        }
        else
        {
            // USB通信モード: USBをポーリングしてコマンドを処理する。
            USB_Comm_Task();
        }
    }

    //電池不足が継続した場合のみここに到達
    cli();
    showError(1);
}

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="割り込みコールバック関数">

// 1秒ごとのコールバック関数 (PIT)
void oneSecHandler(void)
{
    process_logging_flag = true;
}

// RSTボタンのエッジ割り込み (PA0両エッジ)。押下/解放へ即時反応してLEDを点灯/消灯する。
// (5秒長押しによるリセット判定は executeSecondlyTask のポーリングで行う)
void rstButtonHandler(void)
{
    if (!RST_GetValue()) turnOnGreenLED();  // 押し込み(Low) → 点灯
    else                 turnOffGreenLED(); // 解放(High)   → 消灯
}

// </editor-fold>

void executeSecondlyTask(void)
{
    //電圧確認（限界まで使うとFlashメモリのデータが破損することがある）
    if(isLowBattery()) lowBattery_timer++;
    else lowBattery_timer = 0;

    //ソフトウェア時計を1秒進める
    g_epoch++;

    // --- RSTボタン処理 & LED表示 -----------------------------------------
    //  ・押している間 : LED点灯 (計測できているか/生存の確認用)
    //  ・5秒以上長押し: ソフトウェアリセット → USB通信モードに戻る
    //  ・非押し込み時 : USB通信モード=1秒ごとに2回点滅(省電力) / ロギングモード=消灯
    if (!RST_GetValue())  // 押し込み中 (アクティブLow)
    {
        turnOnGreenLED();
        reset_timer++;
        if (reset_timer >= 5)
        {
            while (!RST_GetValue()) ; // ボタンが離されるまで待ってからリセット
            _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
        }
    }
    else
    {
        reset_timer = 0;
        if (isLogging) turnOffGreenLED();          // 計測モード: 消灯
        else           blinkGreenLEDHeartbeat();   // USB通信モード: 2回点滅(省電力)
    }

    // --- 計測トリガ (非ブロッキング, 省電力のためLEDは点滅させない) -------
    // 計測周期 = interval_co2 秒。区間先頭(sec==0)でトリガし、その次tickで読み出して保存する。
    // ・トリガ/読み出しを別tickに分けてSTCC4の500ms待ちをスリープ中に消化(USBブロック排除)。
    // ・読み出しtickも区間内に数えるので、記録間隔は正確に interval_co2 秒(ドリフトしない)。
    // ・満杯(rec_latest>=MAX)に達したら新規トリガを止める(ループ無し・消費電力も抑制)。
    static unsigned int sec = 0;
    if (isLogging && EM_mSettings.interval_co2 > 0)
    {
        if (conditioning_wait > 0)
        {
            // コンディショニング中(約22秒)は計測しない。PITスリープで時間を消化し、
            // センサが他コマンドを受け付ける状態に戻るのを待つ。
            conditioning_wait--;
            sec = 0; // 完了直後の最初のtickで sec==0 → trigger できるように
        }
        else
        {
            if (Measurement_IsPending()) Measurement_ReadStore(); // 前tickのトリガ分を保存
            if (rec_latest < MAX_RECORD_COUNT)                    // 満杯でなければ次区間をトリガ
            {
                if (sec == 0)
                {
                    if (!start_dt_set)
                    {
                        // 1レコード目の時刻基準を「実際に計測を始める今」で確定する。
                        // (コンディショニングの約22秒だけ start_logging 時刻から後ろにずれる)
                        EM_mSettings.start_dt = g_epoch;
                        EM_saveMeasurementSetting();
                        start_dt_set = true;
                    }
                    Measurement_Trigger();
                }
                if (++sec >= EM_mSettings.interval_co2) sec = 0;
            }
        }
    }
    else
    {
        sec = 0; //ロギング停止中はカウンタをリセット
    }
}

// ロギング(計測)の開始/停止。USBコマンド(start_logging/stop_logging)から呼ばれる。
// true でロギングモードに入ると、メインループはSTANDBYスリープ＋PIT定期計測に移行する。
// 再びUSB通信したい場合はリセット(RST 5秒長押し等)でUSB通信モードに戻る。
void setLoggingActive(bool on)
{
    if (on)
    {
        // ロギング開始時にSTCC4初期化。
        // (boot時ではなくここで行うことで、USB通信モードをI2C非依存にし確実に列挙させる)
        Measurement_Initialize();
        // 過去データは保持しない方針なので、毎回フラッシュ先頭(index 0)から記録する。
        // 使用済み領域を消去し rec_latest を 0 に戻す。
        Measurement_ClearData();
        // 長期未通電からの復帰に備え、計測開始前に1度コンディショニング(約22秒)を実施する
        // (STCC4 datasheet 推奨)。コマンド送信のみで即 return し、約22秒は executeSecondlyTask
        // の conditioning_wait カウントダウン中(PITスリープ)で消化してから初回計測へ移る。
        if (Measurement_StartConditioning())
            conditioning_wait = MEASUREMENT_CONDITIONING_SEC;
        else
            conditioning_wait = 0; // 送信失敗時は従来どおり即計測へ(コンディショニング省略)
        // start_dt(各レコードの時刻復元の基準)は、コンディショニング後の初回計測時に確定する。
        // (executeSecondlyTask で start_dt_set を見て EEPROM 保存する)
        start_dt_set = false;
    }
    isLogging = on;
}

bool getLoggingActive(void)
{
    return isLogging;
}

// ソフトウェア時計 (UNIX epoch秒) の設定/取得。set_time / get_status から使う。
void Clock_Set(uint32_t epoch) { g_epoch = epoch; }
uint32_t Clock_Get(void)       { return g_epoch; }

// 電池電圧計測 (PD3 = ADC0 AIN3 に VBAT が直接接続されている前提)
// 基準電圧は VDD (3.3V regulated)、8 sample 積算で精度確保。戻り値: VBAT [mV]
uint16_t getBatteryVoltage_mV(void)
{
    ADC0_SetReferenceVoltage(ADC_VREF_VDD);
    ADC0_SampleRepeatCountSet(ADC_8_SAMPLES_ACCUMULATED);
    ADC0_ChannelSelect(ADC0_CHANNEL_AIN3);
    ADC0_ConversionStart();
    while(!ADC0_IsConversionDone());
    adc_accumulate_t acc_val = ADC0_AccumulatedResultGet();

    // 10bit ADC * 8 回積算 → 0..8192 のレンジ。基準 3.3V (= 3300mV)
    uint32_t battery_mv = (uint32_t)acc_val * 3300 / 8192;
    if (battery_mv > 0xFFFF) battery_mv = 0xFFFF;
    return (uint16_t)battery_mv;
}

// 電池残量が小さくなったか否か。
// 電源は昇圧(TPS61021A, 3.3V出力, 入力0.5〜4.4V/起動0.9V)なので、電池電圧が下がっても
// STCC4等は3.3V固定で動く。よって電池は深くまで使える(旧2.4Vカットオフは無駄が多かった)。
// カットオフは 2.0V とする: 放電試験で 1.5V だと電圧崩落域(~500mV)まで計測を続け、末尾が
// 異常値/欠測になり、かつ崩落電圧でのフラッシュ書込みは破損リスクがあった。放電カーブの
// 「膝」の手前(2.0V)で止めることで、末尾のゴミデータと書込み破損を回避する。
bool isLowBattery(void)
{
    return getBatteryVoltage_mV() < 2000;
}

// 機体固有ID。AVRのシリコンシリアル(SIGROW.SERNUM, 16バイト)を FNV-1a(32bit)で
// ハッシュして返す。ユーザー変更不可・機器ごとに(ほぼ)一意。get_id コマンドが返す。
// (poem プロジェクトと同方式。BACnet制約が無いので32bitのまま使う)
uint32_t Device_GetID(void)
{
    const volatile uint8_t *sn = &SIGROW.SERNUM0; // SERNUM0..15 は連続配置
    uint32_t hash = 2166136261UL;                 // FNV-1a offset basis
    for (uint8_t i = 0; i < 16; i++)
    {
        hash ^= sn[i];
        hash *= 16777619UL;                        // FNV prime
    }
    return hash;
}

// エラー表示 (電池不足): 緑LEDを3秒ごとに1回点滅し続ける (停止状態)。
void showError(short int errNum)
{
    (void)errNum;
    while(true)
    {
        blinkGreenLED(1);
        DELAY_milliseconds(3000);
    }
}
