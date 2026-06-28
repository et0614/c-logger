/* 
 * File:   main.h
 * Author: e.togashi
 *
 * Created on 2025/12/13, 10:13
 */

#ifndef MAIN_H
#define	MAIN_H

#ifdef	__cplusplus
extern "C" {
#endif
    
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "w25q64.h" //内蔵フラッシュ

uint16_t getBatteryVoltage_mV(void);

bool isLowBattery(void);
    
void showError(short int errNum);

void resetButtonHandler(void);

void oneSecHandler(void);

void msecHandler(void);

void executeSecondlyTask(void);

// ロギング(計測)モードの開始/停止と状態取得 (USBコマンドから制御)
void setLoggingActive(bool on);
bool getLoggingActive(void);

void genDummyData(void);

#ifdef	__cplusplus
}
#endif

#endif	/* MAIN_H */

