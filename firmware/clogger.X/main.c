/**
 * @file main.c
 * @brief AVR(AVR64DU32)を使用した温湿度・CO2計測ロギングプログラム
 * @author E.Togashi
 * @date 2026/6/20
 *
 * 動作モード:
 *   USB通信モード (起動時の既定, isLogging=false):
 *     USBをポーリングしてPCコマンドを処理。LEDは省電力のため1秒ごとに短く1回点滅(ハートビート)。
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
#include "mcc_generated_files/usb/usb_device.h" // USBDevice_Initialize
#include "mcc_generated_files/usb/usb_common/usb_core_events.h" // USB_SuspendCallbackRegister
#include "measurement.h"    //温湿度・CO2計測

//標準ヘッダ
#include <avr/io.h>        // RTC / RSTCTRL / PORT レジスタ, _PROTECTED_WRITE
#include <avr/wdt.h>       // _PROTECTED_WRITE 提供のため
#include <avr/sleep.h>     // set_sleep_mode / sleep_mode
#include <avr/interrupt.h> // sei / cli
#include <util/atomic.h>   // ATOMIC_BLOCK

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="広域変数定義">

//1秒毎の処理実行フラグ (PIT割り込みで立つ)
volatile bool process_logging_flag = false;
static volatile bool process_usb_sec = false;  // USB通信モードの1秒ごとの処理 (PIT で立つ)

//電池不足継続タイマ[sec]
static uint8_t lowBattery_timer = 0;

//リセットボタン押し込み継続タイマ[sec]
static uint8_t reset_timer = 0;

//計測(ロギング)モードか否か。false = USB通信モード(既定)。
static bool isLogging = false;

//ソフトウェア時計 (UNIX epoch秒)。set_timeコマンドで設定し、PIT(1秒)の割り込みの中で進める。
//(メインループで進めると、データの読み出しや消去でループが1秒以上ふさがったときに取りこぼす)
//各レコードの時刻は start_dt + index*interval で復元するため start_dt の基準に使う。
static volatile uint32_t g_epoch = 0;

// USB が動いているか (attach してポーリングしている)。フラッシュの消去待ちの間のポーリングに使う。
static bool s_usb_running = false;

//コンディショニング(長期未通電復帰用)の残り待ち秒数。start_logging時にセットし、PIT毎に
//デクリメント。0になるまで計測を始めない(その間はPITスリープで時間を消化する)。
static uint8_t conditioning_wait = 0;

//CO2 センサの暖機の残り秒数。start_logging時にセットし、0になるまで窓の外でも毎秒計測する。
static uint16_t co2_warmup_left = 0;

//start_dt(1レコード目の時刻基準)を確定済みか。コンディショニング完了後の初回計測で確定する。
static bool start_dt_set = false;

//USBが接続された(UDTC の立ち上がり)。メインループでリセットして USB通信モードで起動し直す。
static volatile bool s_usb_plugged = false;

//起動時に USB(VBUS)が接続されていたか。false なら USB は止めたまま(電池のみ)。
static bool s_usb_active = false;

//待機(電池のみ・記録していない)の省電力設定を済ませたか。
static bool s_standby = false;
static void enterStandby(void);

// USB が抜かれたかもしれない (USB の Suspend か UDTC の立ち下がり)。メインループで確かめる
static volatile bool s_usb_check = false;
static void usbSuspendHandler(void);
static void checkUsbUnplugged(void);
static void usbStart(void);
static void usbHeartbeat(void);

// SOF (ホストが 1ms ごとに送る) がこの秒数来なければ、USB が抜かれていないか確かめる。
// 未エニュメレーションのまま放置、PC のスリープ、USB スタックの停止のどれでも働く。
#define USB_NO_SOF_CHECK_SEC  10

// ウォッチドッグ。USB通信モードの間だけ有効にする (フューズでは無効。リセットすると無効に戻る)。
// 待機 (POWER-DOWN) と記録 (STANDBY) では使わない: 起床が増えて電池を消耗するため。
static void watchdogSet(uint8_t period)
{
    while (WDT.STATUS & WDT_SYNCBUSY_bm) ;
    _PROTECTED_WRITE(WDT.CTRLA, period);
}

// 不具合の調査用の記録 (get_diag で返す)。.noinit に置き、ソフトウェアリセットや
// ウォッチドッグのリセットをまたいで残す。電源を入れ直すと消える。
#define DIAG_MAGIC  0xC10Eu  // Diag_t の並びを変えたら変える
static Diag_t s_diag __attribute__((section(".noinit")));

Diag_t *Diag_Get(void) { return &s_diag; }

static void usbResetHandler(void)  { s_diag.bus_resets++; }
static void usbResumeHandler(void) { s_diag.resumes++; }

// フラッシュの書き込み・消去の完了待ちの間に呼ばれる。USB通信モードでは USB のポーリングを続ける
// (消去は1回で最大2秒かかり、その間ホストの要求に応答できなくなるため)。
static void flashBusyHook(void)
{
    if (s_usb_running && !isLogging) USB_Comm_Pump();
}

static void diagBoot(void)
{
    uint8_t rstfr = RSTCTRL.RSTFR;
    RSTCTRL.RSTFR = rstfr; // 読んだ要因を消す (1 を書くと消える)
    if (s_diag.magic != DIAG_MAGIC || (rstfr & (RSTCTRL_PORF_bm | RSTCTRL_BORF_bm)))
    {
        s_diag = (Diag_t){ .magic = DIAG_MAGIC };
    }
    s_diag.rstfr = rstfr;
    if ((rstfr & RSTCTRL_WDRF_bm) && s_diag.wdt_resets < 255) s_diag.wdt_resets++;
}

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="main">

int main(void)
{
    SYSTEM_Initialize();   // クロック / ピン(LED,RST) / ADC / I2C / SPI / USB / RTC 等
    diagBoot();

    // USB(VBUS)が本当に接続されているかを確かめる。
    // SYSTEM_Initialize は USB をバスに attach する。attach すると D+ のプルアップ電圧が
    // ESD保護(USBLC6)のダイオード経由で VBUS に回り込み、電池だけでも UDTC が High(約1.6V)になる。
    // そこで一度 detach して回り込みが抜けるのを待ってから UDTC を読む。
    // attach し直すのはメインループの直前 (この関数の末尾)。attach するとホストは 100ms 余りで
    // バスリセットと最初の要求を送ってくるので、それまでにポーリングを始めていないと、リセットと
    // 要求が溜まって処理の順が逆になる (要求に応答を用意した直後に古いリセットを処理して切断する)。
    USB_Comm_Disable();
    DELAY_milliseconds(200);   // VBUS の放電待ち (VBUS の 1uF と分圧抵抗で時定数 約40ms)
    s_usb_active = UDTC_GetValue() != 0;

    // SPIモジュールを有効化 (内蔵フラッシュ W25Q128 用)
    SPI0_Open(SPI0_DEFAULT);
    W25_SetBusyHook(flashBusyHook);
    // 待機中に Power-down にしたフラッシュは MCU のリセットでは戻らないので、必ず復帰させる
    W25_ReleasePowerDown();

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

    // RSTボタン(PD6)の割り込みで、押下/解放へ即時反応させる(LED点灯/消灯)。
    // プルアップと両エッジ割り込みは MCC で設定済み。
    RST_SetInterruptHandler(rstButtonHandler);

    // USB接続の検出(UDTC=PD2, 両エッジ割り込みは MCC で設定済み。完全非同期ピンなので STANDBY 中でも起床する)。
    // USB の初期化は起動時だけなので、電池で起動した後に USB を挿したらリセットして起動し直す。
    UDTC_SetInterruptHandler(udtcHandler);

    // ロギング中のSTANDBYスリープでもPITで毎秒起床できるよう RUNSTDBY を有効化。
    // (MCC生成 RTC_Initialize は RUNSTDBY 無効で初期化している)
    while (RTC.STATUS > 0) ;
    RTC.CTRLA |= RTC_RUNSTDBY_bm;

    // 保存してある時計の補正値を RTC に設定する (set_clock_calib で設定)
    Clock_SetCalib(EM_calib.clock_ppm);

    DELAY_milliseconds(100);
    sei();
    DELAY_milliseconds(100);

    // USB が接続されていれば、ここで attach して通信を始める (直後からメインループでポーリングする)。
    if (s_usb_active)
    {
        USB_Comm_SetSerial(Device_GetID()); // USB のシリアル番号を機体IDにする (全台同じ "1" だと2台つなげない)
        usbStart();
        watchdogSet(WDT_PERIOD_8KCLK_gc); // 約8秒。USB_Comm_Pump が払う
    }

    // 10秒以上電圧不足が継続したら終了 (フラッシュ書込み中の電池切れによる破損を防ぐ)
    while (lowBattery_timer <= 10)
    {
        // USBが挿された: リセットして USB通信モードで起動し直す(記録中なら記録を終える)。
        // 割り込み内ではなくここで行い、Flash 書込みの途中でリセットしないようにする。
        if (s_usb_plugged) _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);

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
            if (s_usb_active)
            {
                // USB通信モード: USBをポーリングしてコマンドを処理する。
                USB_Comm_Task();
                if (isLogging) continue; // start_logging を受けた: USB は止めてある

                // USB スタックがエラーで止まった (止まると自分では戻らない): 記録して USB を作り直す。
                int8_t fault = USB_Comm_Fault();
                if (fault != 0)
                {
                    s_diag.usb_err = fault;
                    if (s_diag.usb_errs < 255) s_diag.usb_errs++;
                    s_usb_check = true;
                }

                // USB が抜かれたら待機に戻る。抜いても D+ のプルアップが VBUS に回り込んで UDTC が
                // High のままになることがあるので、UDTC の立ち下がりだけでは分からない。
                // 抜けた疑いがあるとき (エニュメレーション後の Suspend / UDTC の立ち下がり /
                // SOF が10秒来ない) に、起動時と同じ方法で確かめる。
                static uint8_t no_sof_sec = 0;
                if (process_usb_sec)
                {
                    process_usb_sec = false;
                    if (USB_Comm_SofSeen()) no_sof_sec = 0;
                    else if (++no_sof_sec >= USB_NO_SOF_CHECK_SEC) { no_sof_sec = 0; s_usb_check = true; }
                }
                if (s_usb_check)
                {
                    s_usb_check = false;
                    checkUsbUnplugged();
                }
            }
            else
            {
                // 待機(電池のみ・記録していない): POWER-DOWN スリープで電池の消耗を抑える。
                // 起きるのはボタン(PD6: 押している間 LED 点灯)と USB 接続(PD2: リセットして USB通信モードへ)だけ。
                // どちらも完全非同期ピンなので POWER-DOWN からでも割り込みで起床できる。
                if (!s_standby) enterStandby();
                set_sleep_mode(SLEEP_MODE_PWR_DOWN);
                sleep_mode();
            }
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
    g_epoch++; // ソフトウェア時計を1秒進める
    process_logging_flag = true;
    process_usb_sec = true;
}

// 待機に入る前の省電力設定 (1回だけ)。待機から記録や USB 通信に戻るときは必ずリセットを経るので、元に戻す処理は不要。
static void enterStandby(void)
{
    turnOffGreenLED();
    RTC.PITINTCTRL = 0;               // 毎秒の起床(PIT)を止める。LED の生存表示もしない
    ADC0.CTRLA &= ~ADC_ENABLE_bm;     // ADC を止める
    W25_PowerDown();                  // フラッシュを Power-down に (待機電流 typ 10uA → 1uA)
    Measurement_SensorPowerDown();    // CO2 センサを sleep に (電源投入後は idle 55uA のまま待っている → 1uA)
    s_standby = true;
}

// UDTC(USB接続検出)のエッジ割り込み。立ち上がり(接続)でフラグを立てる。
void udtcHandler(void)
{
    if (UDTC_GetValue()) s_usb_plugged = true;
    else if (s_usb_active) s_usb_check = true;   // 立ち下がり: 抜かれた
}

// USB の Suspend (バスが 3ms 以上無通信)。抜かれたときと、PC のスリープなどで起きる。
// エニュメレーション前 (アドレス未設定) の Suspend は無視する (未接続中の判定は10秒ごとに行う)。
static void usbSuspendHandler(void)
{
    s_diag.suspends++;
    if (USB0.ADDR != 0) s_usb_check = true;
}

// USB がまだ接続されているかを確かめる。detach して VBUS への回り込みが抜けるのを待ってから UDTC を読む。
// 抜けていればリセットして起動し直す (電池のみの起動になり、待機の POWER-DOWN に入る)。
// つながっていれば (PC のスリープなど) attach し直す。
static void checkUsbUnplugged(void)
{
    if (s_diag.checks < 255) s_diag.checks++;
    s_usb_running = false;
    USB_Comm_Disable();
    DELAY_milliseconds(200);
    if (!UDTC_GetValue()) _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
    usbStart();
}

// USB を初期化して attach する (USB_Comm_Disable で止めた後の作り直しにも使う)。
// 呼んだらすぐにポーリング (USB_Comm_Task) を始めること。
static void usbStart(void)
{
    USBDevice_Initialize();
    USB_Comm_Initialize();
    USB_SuspendCallbackRegister(usbSuspendHandler);
    USB_ResetCallbackRegister(usbResetHandler);
    USB_ResumeCallbackRegister(usbResumeHandler);
    s_usb_running = true;
}

// USB通信モードの生存表示: 1秒ごとに短く1回点滅する。点灯している間も USB のポーリングを続ける
// (止めると、その間に来たバスリセットと要求の処理の順が逆になることがある)。
static void usbHeartbeat(void)
{
    turnOnGreenLED();
    for (uint8_t i = 0; i < LED_HB_PULSE_MS; i++)
    {
        if (s_usb_active) USB_Comm_Pump();
        DELAY_milliseconds(1);
    }
    turnOffGreenLED();
}

// RSTボタンのエッジ割り込み (PD6両エッジ)。押下/解放へ即時反応してLEDを点灯/消灯する。
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

    // --- RSTボタン処理 & LED表示 -----------------------------------------
    //  ・押している間 : LED点灯 (計測できているか/生存の確認用)
    //  ・5秒以上長押し: ソフトウェアリセット → USB通信モードに戻る
    //  ・非押し込み時 : USB通信モード=1秒ごとに1回点滅(省電力) / ロギングモード=消灯
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
        else           usbHeartbeat();             // USB通信モード: 1秒ごとに1回点滅(省電力)
    }

    // --- 計測 (非ブロッキング, 省電力のためLEDは点滅させない) -------------
    // 計測周期 = interval 秒。各区間の「末尾 CO2_AVG_WINDOW_SEC 秒」だけ連続 single-shot し、
    // その CO2 平均を1レコードとして区間境界(sec==0)で保存する。温湿度は平均化せず窓内最後の値。
    //  ・窓開始(sec==win_start)で Begin、以降の中間tickで Sample、境界で Store のパイプライン。
    //   → single-shot の500ms待ちは常にスリープ中に消化される(USBブロック無し)。
    //  ・実効窓 w = min(CO2_AVG_WINDOW_SEC, 間隔)。間隔が窓より短ければ区間全体で平均。
    //  ・記録の時刻は「区間の終端」に対応する。start_dt は最初の保存時点で確定する。
    //  ・満杯(rec_latest>=MAX)に達したら計測を止める(ループ無し・消費電力も抑制)。
    static unsigned int sec = 0;
    if (isLogging && EM_settings.interval > 0)
    {
        if (conditioning_wait > 0)
        {
            // コンディショニング中(約22秒)は計測しない。PITスリープで時間を消化し、
            // センサが他コマンドを受け付ける状態に戻るのを待つ。
            conditioning_wait--;
            sec = 0; // 完了直後、最初の窓開始から計測を始められるように
        }
        else if (rec_latest < MAX_RECORD_COUNT) // 満杯でなければ計測を継続
        {
            uint16_t N = EM_settings.interval;
            uint16_t w = (CO2_AVG_WINDOW_SEC < N) ? CO2_AVG_WINDOW_SEC : N; // 実効窓 = min(窓,間隔)
            uint16_t win_start = N - w;

            // CO2 センサの暖機中 (記録開始から CO2_WARMUP_SEC 秒) は、窓の外でも毎秒計測を続ける。
            bool warming = co2_warmup_left > 0;
            if (warming) co2_warmup_left--;

            // 区間境界: 前区間の窓を締めて平均を保存する(最初の区間はまだ窓が無いのでpending時のみ)。
            if (sec == 0 && Measurement_IsPending())
            {
                if (!start_dt_set)
                {
                    // 1レコード目の時刻基準を「最初の保存(=区間終端)の今」で確定する。
                    // 以降の各レコード時刻は start_dt + index*interval で復元される。
                    EM_settings.start_dt = Clock_Get();
                    EM_saveSettings();
                    start_dt_set = true;
                }
                Measurement_AvgStore();
                // 次の窓まで間があれば CO2 センサを sleep にする (idle 55uA → 1uA)。暖機中は眠らせない
                if (!warming && win_start > 0) Measurement_SensorSleep();
            }

            // 窓の開始/中間: 連続 single-shot で CO2 を積算する。
            if (sec == win_start)      Measurement_AvgBegin();  // 窓開始(リセット+トリガ)
            else if (sec > win_start)  Measurement_AvgSample();  // 窓中間(読み+積算, 次トリガ)
            else if (warming)          Measurement_WarmupShot(); // 窓の外: 暖機のための計測 (結果は捨てる)
            // それ以外の sec < win_start はスリープのみ(計測しない=省電力)

            if (++sec >= N) sec = 0;
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
        // 記録中はウォッチドッグを使わない (この後 USB を止めて STANDBY で眠るため)
        watchdogSet(0);
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
        // CO2あり版は、記録間隔によらず最初の1時間は毎秒計測してセンサを暖機する
        co2_warmup_left = Measurement_HasCO2() ? CO2_WARMUP_SEC : 0;
        // start_dt(各レコードの時刻復元の基準)は、コンディショニング後の初回計測時に確定する。
        // (executeSecondlyTask で start_dt_set を見て EEPROM 保存する)
        start_dt_set = false;
    }
    // 記録中はフラッシュを書き込みの合間に Power-down にする (止めるときは起こす)
    Measurement_SetFlashSleep(on);
    isLogging = on;
}

bool getLoggingActive(void)
{
    return isLogging;
}

// ソフトウェア時計 (UNIX epoch秒) の設定/取得。set_time / get_status から使う。
// 割り込みの中で進めるので、4バイトの読み書きは割り込みを止めて行う。
void Clock_Set(uint32_t epoch)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { g_epoch = epoch; }
}

uint32_t Clock_Get(void)
{
    uint32_t e;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { e = g_epoch; }
    return e;
}

// 時計の補正 [ppm] (正=水晶が速いので遅らせる)。RTC の水晶誤差補正 (CALIB/CORREN) を使う。
// 100万クロックごとに |ppm| クロックを足し引きし、PIT(1秒)にも効く。
// 進める向き(負)の補正にはプリスケーラ DIV2 以上が必要。RTC のカウンタは使っていないので DIV2 にしておく
// (PIT はプリスケーラの前段から取るので影響しない)。
static int8_t s_clock_calib = 0;

void Clock_SetCalib(int8_t ppm)
{
    if (ppm < -127) ppm = -127;
    s_clock_calib = ppm;
    while (RTC.STATUS > 0) ;
    RTC.CALIB = (ppm >= 0) ? (uint8_t)ppm : (uint8_t)(RTC_SIGN_bm | (uint8_t)(-ppm));
    while (RTC.STATUS > 0) ;
    uint8_t a = (RTC.CTRLA & ~(RTC_PRESCALER_gm | RTC_CORREN_bm)) | RTC_PRESCALER_DIV2_gc;
    if (ppm != 0) a |= RTC_CORREN_bm;
    RTC.CTRLA = a;
}

int8_t Clock_GetCalib(void) { return s_clock_calib; }

// 電池電圧計測 (PF5 = ADC0 AIN21 に VBAT が直接接続されている前提)
// 基準電圧は VDD (3.3V regulated)、8 sample 積算で精度確保。戻り値: VBAT [mV]
uint16_t getBatteryVoltage_mV(void)
{
    ADC0_SetReferenceVoltage(ADC_VREF_VDD);
    ADC0_SampleRepeatCountSet(ADC_8_SAMPLES_ACCUMULATED);
    ADC0_ChannelSelect(VBAT);
    // 完了は結果のフラグ (RESRDY) で待つ。変換中フラグ (ADCBUSY) は変換を始めた直後にはまだ立っていない
    // ことがあり、それで待つと前回の結果を読んでしまう。
    ADC0.INTFLAGS = ADC_RESRDY_bm;            // 前回の結果のフラグを消す
    ADC0_ConversionStart();
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)); // 8回の積算が終わるまで待つ
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
    // USB給電中(UDTC=High)は電池を見ない。電池が無くても動き、そのとき VBAT は 0V になるため。
    if (UDTC_GetValue()) return false;
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

// エラー表示 (電池不足): LEDを3秒ごとに1回点滅し続ける (停止状態)。
// 点滅の合間は STANDBY で眠り、PIT(1秒)で起きる。眠らずに回り続けると数mA流れ続け、
// 弱った電池をさらに深く放電させてしまう (液漏れの原因になる)。
void showError(short int errNum)
{
    (void)errNum;
    watchdogSet(0);

    // 省電力の設定: USB・ADC を止め、フラッシュを Power-down にする
    s_usb_running = false;
    USB_Comm_Disable();
    ADC0.CTRLA &= ~ADC_ENABLE_bm;
    W25_PowerDown();
    Measurement_SensorPowerDown();

    // PIT(1秒)で STANDBY から起きられるようにする (起動時の電池確認から呼ばれたときは未設定)
    while (RTC.STATUS > 0) ;
    RTC.CTRLA |= RTC_RUNSTDBY_bm;
    RTC.PITINTCTRL = RTC_PI_bm;
    sei();

    while(true)
    {
        blinkGreenLED(1);
        // 待つ間に USB が接続されたら(UDTC=High)リセットして USB通信モードで起動し直す。
        // USB給電なら電池を見ないので、電池不足でも PC から状態確認やデータ回収ができる。
        // (UDTC の立ち上がりでも起きるので、接続にはすぐ反応する)
        for (uint8_t i = 0; i < 3; i++)
        {
            if (UDTC_GetValue()) _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
            set_sleep_mode(SLEEP_MODE_STANDBY);
            sleep_mode();
        }
    }
}
