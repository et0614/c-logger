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
/// 内部で約500msブロックするので、USB等を止めたくない場合は下の非ブロッキングAPIを使う。
/// @return true:保存成功, false:計測失敗 or Flash 満杯
bool Measurement_Record(void);

// ====== 非ブロッキング計測 (USBポーリングを止めないため) ======
// single-shot のトリガと読み出しを分離する。トリガ後 500ms 以上経過してから
// Measurement_ReadStore() を呼ぶこと (PIT 1秒周期なら「次のtick」で読めばよい)。

/// single-shot 計測をトリガする (即 return)。@return true:トリガ成功
bool Measurement_Trigger(void);

/// トリガ済みの計測があるか (= 読み出し待ちか)
bool Measurement_IsPending(void);

/// トリガ済みの計測結果を読み出して Flash に保存し、rec_latest を1進める。
/// トリガから500ms以上経過してから呼ぶこと。pending でなければ何もしない。
/// @return true:保存成功, false:未トリガ/計測失敗/Flash満杯
bool Measurement_ReadStore(void);

/// 保存済みデータを消去する (メモリ初期化)。使用済みセクタ(4KB単位)を物理消去し、
/// rec_latest を 0 に戻す。空き判定(humidity==0xFFFF)が正しく働くよう物理消去する。
/// 注意: 消去はブロッキング(セクタあたり約45ms)。データ量が多いと時間がかかる。
void Measurement_ClearData(void);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_H */
