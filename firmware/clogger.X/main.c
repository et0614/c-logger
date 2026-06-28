/**
 * @file main.c
 * @brief AVR(AVR64DU32)を使用した温湿度・CO2計測ロギングプログラム
 * @author E.Togashi
 * @date 2026/6/20
 *
 * 動作モード:
 *   USB通信モード (起動時の既定, isLogging=false):
 *     USBをポーリングしてPCコマンドを処理。LEDを1秒ごとにOn/Off(ハートビート)。
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
    //  ・非押し込み時 : USB通信モード=1秒ごとにOn/Off / ロギングモード=消灯(省電力)
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
        if (isLogging) turnOffGreenLED();    // 計測モード: 消灯
        else           turnToggleGreenLED(); // USB通信モード: 1秒ごとにOn/Off
    }

    // --- 計測トリガ (非ブロッキング, 省電力のためLEDは点滅させない) -------
    // 計測周期 = interval_co2 秒。区間先頭(sec==0)でトリガし、その次tickで読み出して保存する。
    // ・トリガ/読み出しを別tickに分けてSTCC4の500ms待ちをスリープ中に消化(USBブロック排除)。
    // ・読み出しtickも区間内に数えるので、記録間隔は正確に interval_co2 秒(ドリフトしない)。
    // ・満杯(rec_latest>=MAX)に達したら新規トリガを止める(ループ無し・消費電力も抑制)。
    static unsigned int sec = 0;
    if (isLogging && EM_mSettings.interval_co2 > 0)
    {
        if (Measurement_IsPending()) Measurement_ReadStore(); // 前tickのトリガ分を保存
        if (rec_latest < MAX_RECORD_COUNT)                    // 満杯でなければ次区間をトリガ
        {
            if (sec == 0) Measurement_Trigger();
            if (++sec >= EM_mSettings.interval_co2) sec = 0;
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
        // 計測開始日時を現在のソフトウェア時計で記録(EEPROM保存)。
        // dump時に各レコードの時刻を start_dt + index*interval_co2 で復元する。
        EM_mSettings.start_dt = g_epoch;
        EM_saveMeasurementSetting();
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

// 電池残量が小さくなったか否か (CR2032は3.0V。2.4Vで危険と判断する)
bool isLowBattery(void)
{
    return getBatteryVoltage_mV() < 2400;
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
