/**
 * @file main.c
 * @brief AVR(AVR64DU32)を使用した温湿度・CO2計測ロギングプログラム
 * @author E.Togashi
 * @date 2026/6/20
 */

// <editor-fold defaultstate="collapsed" desc="ヘッダインクルード">

//自動生成ヘッダ
#include "mcc_generated_files/system/clock.h" //F_CPUの設定
#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/adc/adc0.h" // ADC（電圧計測）
#include "mcc_generated_files/timer/delay.h"
#include "mcc_generated_files/timer/tca0.h" // 1ms周期タイマ (IDLE起床用)
#include "mcc_generated_files/usb/usb_common/usb_core_descriptors.h" // 列挙状態の確認

//自作ヘッダ
#include "main.h"
#include "hal_io.h"
#include "adc0_extension.h" //AD変換拡張
#include "eeprom_manager.h" //EEPROM
#include "usb_comm.h"       //USB-CDC通信 (PCコマンド)
#include "measurement.h"    //温湿度・CO2計測

//標準ヘッダ
#include <avr/sleep.h>
#include <avr/wdt.h>     // _PROTECTED_WRITE for WDT.CTRLA (XBEE_QUIET_MODE 用)
#include <avr/io.h>      // USART0 register access (XBEE_QUIET_MODE 用)
#include <avr/interrupt.h> // sei / cli

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="広域変数定義">

//1秒毎の処理実行フラグ
volatile bool process_logging_flag = false;

//風速の電圧計測フラグ
volatile bool velocity_adc_flag = false;

//稼働秒数 (ready イベント の uptime_s)
static uint32_t uptime_s = 0;

//電池不足継続タイマ[sec]
static uint8_t lowBattery_timer = 0;

//リセット処理用タイマ[sec]
static uint8_t reset_timer = 0;

static bool isLogging = false;

//ソフトウェア時計 (UNIX epoch秒)。set_timeコマンドで設定し、PIT(1秒)毎にインクリメント。
//各レコードの時刻は start_dt + index*interval で復元するため、start_dt の基準に使う。
static uint32_t g_epoch = 0;

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="main">

// ============================================================
// XBEE_QUIET_MODE: 1 にすると USART0 を完全に無効化し、XBee へ一切の通信を行わない。
// AVR は LED 点滅だけして待機する (Digi 公式アプリ等で XBee の BD を BLE 経由で
// 書き換える間、AVR からのノイズで XBee が乱れるのを防ぐ目的)。
//
// 使い方:
//   1. このフラグを 1 にしてビルド → AVR に書き込み → LED が点滅し始める
//   2. Digi XBee Mobile (BLE) や XCTU で XBee の BD を 7 (115200) に変更
//   3. フラグを 0 に戻して USART0 baud も 115200 に設定してビルド → 書き込み
//   4. 通常動作に復帰
// ============================================================
#define XBEE_QUIET_MODE  0

// ============================================================
// DISABLE_LOWBATT_GUARD: 1 にすると低電圧自動停止(showError)を無効化する。
// 起動時ガード(USB初期化前で固まる)と、メインループの低電圧脱出(showErrorで停止)の両方を
// スキップする。USB/ベンチ給電で電池(AIN3)が無い・不足する状態でテストするための【一時設定】。
// 実運用ビルドでは必ず 0 に戻すこと。
// ============================================================
#define DISABLE_LOWBATT_GUARD  1

// USB_IDLE_SLEEP: 1 = USB通信モードで1ms(TCA0)ごとにIDLE間欠スリープ(省電力)。
//                 0 = スリープせずビジーループ(高負荷だがUSBを最速ポーリング=確実)。
// USBが認識されない切り分け用: まず 0(ビジーループ)で列挙するか確認 → 列挙するなら
// IDLEポーリングが原因(1ms周期を詰める等)。0でも列挙しないなら別要因(電源/配線)。
#define USB_IDLE_SLEEP  0

int main(void)
{
    SYSTEM_Initialize();

    //風速ADCの初期化
    ADC_VEL_Initialize();

    // SPIモジュールを有効化
    SPI0_Open(SPI0_DEFAULT);

    //EEPROMロード
    EM_loadEEPROM();

    // 計測の初期化(STCC4のI2C初期化 + rec_latest復元)は boot時には行わない。
    // boot時にI2Cを叩くと、STCC4無応答時に i2c の完了待ちでハングし USB初期化に
    // 到達できず「USB認識されない」状態になる。start_logging受信時(setLoggingActive)に初期化する。
        
    //低電圧が1秒継続したら電池残量エラー
#if !DISABLE_LOWBATT_GUARD
	int count = 0;
	while(isLowBattery()){
		count++;
		if(10 <= count)	showError(1);
        DELAY_milliseconds(100);
	}
#endif
    
    // イベントハンドラ登録
    RTC_SetPITIsrCallback(oneSecHandler); // 1sec割り込み
    RST_SetInterruptHandler(resetButtonHandler); // リセットスイッチ押し込み割り込み

    // ロギング中のSTANDBYスリープでもPITで毎秒起床できるよう RUNSTDBY を有効化。
    // (MCC生成 RTC_Initialize は RUNSTDBY 無効で初期化している)
    while (RTC.STATUS > 0) ;
    RTC.CTRLA |= RTC_RUNSTDBY_bm;
    
    //割り込み許可
	DELAY_milliseconds(100); //割り込み前の待機
    sei();

    //初期設定が終わったら少し待機
	DELAY_milliseconds(100);
    
    // USB仮想シリアル通信の初期化 (CDC層。USBデバイス層はSYSTEM_Initializeで初期化済み)
    USB_Comm_Initialize();

    // USB通信モードのIDLE間欠スリープからの起床には、MCCで設定したTCA0の
    // オーバーフロー割り込み(1ms周期, ISR=TCA0_OVF_vect, MCCがフラグclear)を使う。
    // ※MCC側で「Normalモード/周期1ms/Overflow割り込み有効」を設定しておくこと。
    // TCA0_Initialize() は SYSTEM_Initialize 内で呼ばれている。念のため開始を明示する。
    TCA0_Start();
        
    //10秒以上電圧不足時間が継続したら終了
    while (lowBattery_timer <= 10)
    {            
		//1秒毎の処理を実施
		if(process_logging_flag)
        {
			process_logging_flag = false;
			executeSecondlyTask();
		}

        //ロギング中はスリープ(STANDBY)。USB通信モード中は起きてUSBを処理。
        if(isLogging)
        {
            // ロギングモード: STANDBYスリープし、PIT(1秒)で起床して計測する。
            // USBは処理しない(省電力。CR2032ではUSBアクティブ連続稼働は不可のため)。
            set_sleep_mode(SLEEP_MODE_STANDBY);
            sleep_mode();
        }
        else
        {
            // USB通信モード: 起動直後の既定状態。USBをポーリングしてコマンドを処理する。
            // 計測はしないので軽負荷。start_logging受信で上の分岐へ移る。
            USB_Comm_Task();

#if USB_IDLE_SLEEP
            // IDLEスリープは「列挙完了(configured)後のみ」行う。
            // 列挙中(デバイス記述子要求などの制御転送中)は、1msポーリングでは応答が遅く
            // 「デバイス記述子要求の失敗」になるため、ビジーループのまま高速応答させる。
            // 列挙が完了したら、TCA0(1ms)で起きるIDLEに入って平均電流を下げる。
            if (USB_DescriptorActiveConfigurationValueGet() == 1)
            {
                set_sleep_mode(SLEEP_MODE_IDLE);
                sleep_mode();
            }
#endif
        }
    }
    
    //電池が不足した場合のみ、ここまでたどり着く
	cli(); //割り込み終了
	showError(1); //LED表示
}

// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="割り込みコールバック関数">

// 1秒ごとのコールバック関数
void oneSecHandler(void)
{
	//1秒毎の処理フラグを立てる
	process_logging_flag = true;
}

// リセットスイッチ操作時のコールバック関数
void resetButtonHandler(void)
{
    //Push:LED点灯, None:LED消灯
    if(RST_GetValue()) turnOffGreenLED();
	else turnOnGreenLED();
}

// </editor-fold>

void executeSecondlyTask(void)
{
    //電圧確認（限界まで使うとFlashメモリのデータが破損することがある）
#if !DISABLE_LOWBATT_GUARD
	if(isLowBattery()) lowBattery_timer++;
	else lowBattery_timer = 0;
#endif

	//リセットボタン押し込み確認********************************
	if(!RST_GetValue())
	{
		reset_timer++;
		if(reset_timer == 3)
		{
			//Endlessロギングを解除
			EM_mSettings.start_auto = false;
			EM_saveMeasurementSetting();
            blinkGreenLED(3);	//緑LED点滅

            // ボタンが解放されるまで待ってから SWR を発行する。
            // euboot (USB bootloader) は起動時に PF2 (RST) が Low なら bootloader モードに
            // 入る設計のため、SWR を発行した瞬間にユーザがまだ RST を押し続けていると、
            // アプリ復帰のはずが意図せず bootloader に突入してしまう。これを防ぐ。
            while (!RST_GetValue()) ;

            // マイコン自体をソフトウェアリセット
            _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
		}
	}
	else reset_timer = 0; //Resetボタン押し込み時間を0に戻す
	
    //稼働秒数・ソフトウェア時計を1秒進める
    uptime_s++;
    g_epoch++;

    //計測トリガ (非ブロッキング)*********************************
    // STCC4 single-shot の「トリガ」と「読み出し」を別tickに分割し、500msブロックを排除する。
    // (ブロックするとUSBポーリングが止まり、列挙/通信が失敗するため)
    //   1) interval_co2 秒ごとに Measurement_Trigger() でトリガ (即return)
    //   2) 次tick(1秒後 = 500ms以上経過)で Measurement_ReadStore() が読み出して保存
    // interval_co2 を計測間隔として使う (CO2・温湿度は STCC4 で同時取得するため共通間隔)。
    static unsigned int sec_since_meas = 0;
    if (Measurement_IsPending())
    {
        //前tickでトリガした計測を読み出して保存 (成功で緑LED明滅)
        if (Measurement_ReadStore()) blinkGreenLED(1);
    }
    else if (isLogging && EM_mSettings.interval_co2 > 0)
    {
        if (++sec_since_meas >= EM_mSettings.interval_co2)
        {
            sec_since_meas = 0;
            Measurement_Trigger(); //次tickで読み出し
        }
    }
    else
    {
        sec_since_meas = 0; //ロギング停止中はカウンタをリセット
    }
}

// ロギング(計測)の開始/停止。USBコマンド(start_logging/stop_logging)から呼ばれる。
// true でロギングモードに入ると、メインループはSTANDBYスリープ＋PIT定期計測に移行し、
// USB通信は止まる(省電力)。再びUSB通信したい場合はリセットでUSB通信モードに戻る。
void setLoggingActive(bool on)
{
    if (on)
    {
        // ロギング開始時にSTCC4初期化 + rec_latest を Flash から復元する。
        // (boot時ではなくここで行うことで、USB通信モードをI2C非依存にし確実に列挙させる)
        Measurement_Initialize();
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
// 基準電圧は VDD (3.3V regulated)、8 sample 積算で精度確保。
// 戻り値: VBAT [mV]
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

//エラー表示
void showError(short int errNum)
{	
	switch(errNum){
		case 1: //電池不足
			while(true)
			{
				blinkGreenLED(1);
				DELAY_milliseconds(3000);
			}
		case 2: //XBee設定エラー
			while(true)
			{
				blinkGreenLED(2);
				DELAY_milliseconds(3000);
			}
	}
}
