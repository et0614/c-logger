/* 
 * File:   hal_io.h (Hardware Abstraction Layer)
 * Author: e.togashi
 *
 * Created on 2026/01/01, 6:53
 */

#ifndef HAL_IO_H
#define	HAL_IO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/system/clock.h" //F_CPUの設定
#include <util/delay.h>

//LED点滅設定
#define LED_ON_DURATION_MS  100 // 点灯時間
#define LED_OFF_DURATION_MS 25  // 消灯時間
    
static inline void turnOnGreenLED(void)
{
    LED_SetHigh();
}

static inline void turnOffGreenLED(void)
{
    LED_SetLow();
}

static inline void turnToggleGreenLED(void)
{
    LED_Toggle();
}

static inline void blinkGreenLED(int iterNum)
{
	if(iterNum < 1) return;

	// 一度消灯してから指定回数の点滅
    turnOffGreenLED();
	for(int i=0; i < iterNum; i++)
	{
		_delay_ms(LED_ON_DURATION_MS);
        turnOnGreenLED();
		_delay_ms(LED_OFF_DURATION_MS);
        turnOffGreenLED();
	}
}

// USB通信モードの生存表示用ハートビート: 1秒ごとに短く1回点滅する。
// ブートローダ (euboot) の待機中は 0.67 秒ごとに短く2回点滅するので、見分けられるよう1回にしている。
// 短いパルスで平均点灯を ~20ms/s に抑えて省電力化する。ブロッキングも短く、USB 応答への影響は小さい。
#define LED_HB_PULSE_MS  20  // 点灯時間
static inline void blinkGreenLEDHeartbeat(void)
{
    turnOnGreenLED();
    _delay_ms(LED_HB_PULSE_MS);
    turnOffGreenLED();
}

#ifdef	__cplusplus
}
#endif

#endif	/* HAL_IO_H */

