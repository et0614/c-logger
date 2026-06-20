/*
 * File:   measurement.h
 * Author: e.togashi
 *
 * 温湿度・CO2 の計測処理。STCC4 (CO2センサ) の single-shot 計測で CO2・温度・湿度を
 * まとめて取得し (温湿度は STCC4 が AHT4 を介して返す)、SensorData_t に変換して
 * 内蔵 Flash (W25Q64) に1レコードずつ保存する。
 *
 * 時刻はレコードに持たせない。計測開始日時(start_dt)と間隔(interval)は EEPROM に
 * 記録しておき、各レコードの時刻は start_dt + index*interval で復元する。
 */
#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "w25q64.h"   // SensorData_t / MAX_RECORD_COUNT

/// 次に書き込むレコードの index (= 現在の有効レコード数)。
/// 起動時に W25_Count_Record() で Flash から復元する。dump / 件数取得でも参照する。
extern uint32_t rec_latest;

/// センサ初期化 + rec_latest を Flash から復元する。
/// SYSTEM_Initialize() / SPI0_Open() の後に1回呼ぶこと。
void Measurement_Initialize(void);

/// 1回計測して結果を out に詰める (Flash には書き込まない)。
/// @return true:成功, false:通信/CRCエラー
bool Measurement_Sample(SensorData_t *out);

/// 1回計測して Flash の rec_latest 位置に保存し、rec_latest を1進める。
/// @return true:保存成功, false:計測失敗 or Flash 満杯
bool Measurement_Record(void);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_H */
