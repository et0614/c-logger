/*
 * File:   eeprom_manager.h
 * Author: e.togashi
 *
 * EEPROM に保存する設定と校正値。
 *
 *   計測設定 (EM_settings)   : 記録間隔、記録開始日時。記録を始めるたびに書き換える
 *   校正値   (EM_calib)      : 温度・湿度・CO2 の線形補正係数、時計の補正値。機器ごとに一度合わせる
 *
 * 2つのブロックに分け、それぞれに CRC を付ける (記録開始のたびに書く計測設定が壊れても、校正値は残る)。
 * CRC が合わないブロックは既定値に戻す。EEPROM の並びを変えたら EM_LAYOUT_VERSION を上げる
 * (起動時に全体を既定値で初期化し直す)。
 */

#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// 線形補正: 補正後 = a × 生値 + b
// 小数を使わずに保存・送受信するため、固定小数点で持つ。
#define EM_CORR_A_SCALE  100000L   // a ×100000 (100000 = 1.0。分解能 0.00001)
#define EM_CORR_B_SCALE  100L      // b ×100 (温度 ℃、湿度 %RH、CO2 ppm。分解能 0.01)

typedef struct {
    int32_t a;   // 傾き ×EM_CORR_A_SCALE
    int32_t b;   // 切片 ×EM_CORR_B_SCALE
} LinearCorr;

// 計測設定
typedef struct {
    uint16_t interval;  // 記録間隔 [秒]
    uint32_t start_dt;  // 記録開始日時 (UNIX秒)。各記録の時刻は start_dt + index × interval
    uint8_t  crc;
} MeasurementSettings;

// 校正値
// 温度・湿度・CO2 の補正はフラッシュの記録には適用しない (記録は生値)。PCツールが読み出すときに適用する。
// 係数を後から直しても過去のデータに使え、生値も残る。
typedef struct {
    LinearCorr temp;       // 温度 [℃]
    LinearCorr hum;        // 相対湿度 [%RH]
    LinearCorr co2;        // CO2 [ppm]
    int8_t     clock_ppm;  // 時計の補正値 [ppm] (正=水晶が速いので遅らせる)
    uint8_t    crc;
} Calibration;

extern MeasurementSettings EM_settings;
extern Calibration EM_calib;

// EEPROM を読み込む (起動時に1回)。未初期化・並びの変更・CRC 不一致なら既定値にする
void EM_loadEEPROM(void);

// 計測設定を保存する
void EM_saveSettings(void);

// 校正値を保存する
void EM_saveCalibration(void);

#ifdef __cplusplus
}
#endif

#endif /* EEPROM_MANAGER_H */
