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

int main(void)
{
    SYSTEM_Initialize();

    //風速ADCの初期化
    ADC_VEL_Initialize();

    // SPIモジュールを有効化
    SPI0_Open(SPI0_DEFAULT);

    //EEPROMロード
    EM_loadEEPROM();

    //計測の初期化 (STCC4初期化 + rec_latest を Flash から復元)
    Measurement_Initialize();
        
    //低電圧が1秒継続したら電池残量エラー
	int count = 0;
	while(isLowBattery()){
		count++;
		if(10 <= count)	showError(1);
        DELAY_milliseconds(100);
	}
    
    // イベントハンドラ登録
    RTC_SetPITIsrCallback(oneSecHandler); // 1sec割り込み
    RST_SetInterruptHandler(resetButtonHandler); // リセットスイッチ押し込み割り込み
    
    //割り込み許可
	DELAY_milliseconds(100); //割り込み前の待機
    sei();

    //初期設定が終わったら少し待機
	DELAY_milliseconds(100);
    
    // USB仮想シリアル通信の初期化 (CDC層。USBデバイス層はSYSTEM_Initializeで初期化済み)
    USB_Comm_Initialize();
        
    //10秒以上電圧不足時間が継続したら終了
    while (lowBattery_timer <= 10)
    {            
		//1秒毎の処理を実施
		if(process_logging_flag)
        {
			process_logging_flag = false;
			executeSecondlyTask();
		}

        //ロギング中はスリープ
        if(isLogging)
        {
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_mode();
        }
        else
        {
            //USB通信タスク (スタック駆動 + コマンド受信→JSONディスパッチ)
            USB_Comm_Task();
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
	if(isLowBattery()) lowBattery_timer++;
	else lowBattery_timer = 0;

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
	
    //稼働秒数を進める
    uptime_s++;

    //計測トリガ************************************************
    // ロギング有効 (start_auto) かつ interval>0 のとき、interval 秒ごとに1レコード保存する。
    // 起動/ロギング開始直後の最初の1秒で即1サンプル取得し、以降 interval 秒ごとに計測。
    // interval_co2 を計測間隔として使う (CO2・温湿度は STCC4 で同時取得するため共通間隔)。
    static unsigned int sec_since_meas = 0;
    if (EM_mSettings.start_auto && EM_mSettings.interval_co2 > 0)
    {
        if (sec_since_meas == 0)
        {
            //計測・保存に成功したら緑LEDを1回明滅させて可視化
            if (Measurement_Record()) blinkGreenLED(1);
        }
        sec_since_meas++;
        if (sec_since_meas >= EM_mSettings.interval_co2) sec_since_meas = 0;
    }
    else
    {
        sec_since_meas = 0; //ロギング停止中はカウンタをリセット
    }
}

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
