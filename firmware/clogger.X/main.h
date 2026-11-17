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

// 機体固有ID (AVRシリコンシリアル由来, 読み取り専用)
uint32_t Device_GetID(void);
    
void showError(short int errNum);

void oneSecHandler(void);

void rstButtonHandler(void);
void udtcHandler(void);

// 時計の補正 [ppm] (正=水晶が速い)。RTC に設定する (保存はしない)
void Clock_SetCalib(int8_t ppm);
int8_t Clock_GetCalib(void);

void executeSecondlyTask(void);

// ロギング(計測)モードの開始/停止と状態取得 (USBコマンドから制御)
void setLoggingActive(bool on);
bool getLoggingActive(void);

// ソフトウェア時計 (UNIX epoch秒) の設定/取得
void     Clock_Set(uint32_t epoch);
uint32_t Clock_Get(void);

// 不具合の調査用の記録 (get_diag コマンドで返す)。電源を入れ直すまで残る。
typedef struct {
    uint16_t magic;       // 有効な記録である印
    uint8_t  rstfr;       // 今回の起動のリセット要因 (RSTCTRL.RSTFR。ブートローダが消していれば 0)
    int8_t   usb_err;     // USB スタックが最後に止まったときのエラーコード (RETURN_CODE_t。無ければ 0)
    uint8_t  usb_errs;    // USB スタックが止まって作り直した回数
    uint8_t  wdt_resets;  // ウォッチドッグでリセットした回数
} Diag_t;
const Diag_t *Diag_Get(void);

#ifdef	__cplusplus
}
#endif

#endif	/* MAIN_H */

