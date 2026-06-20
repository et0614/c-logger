#include "measurement.h"
#include "stcc4.h"
#include "w25q64.h"
#include "mcc_generated_files/timer/delay.h"

// STCC4 single-shot 計測の完了待ち時間 [ms] (datasheet: 約500ms)
#define STCC4_SINGLE_SHOT_MS  500

// 次に書き込むレコード index (= 有効レコード数)
uint32_t rec_latest = 0;

void Measurement_Initialize(void)
{
    // STCC4 ソフトリセット (general call) で既知の状態にする
    STCC4_initialize();
    DELAY_milliseconds(10);

    // Flash に保存済みのレコード数を復元 → 次の書き込み位置
    rec_latest = W25_Count_Record();
}

bool Measurement_Sample(SensorData_t *out)
{
    uint16_t co2;
    float    temp_c;   // 温度 [℃]
    float    humi_pct; // 相対湿度 [%RH]

    // single-shot 計測をトリガ → 完了待ち → 結果読み出し
    if (!STCC4_measureSingleShot()) return false;
    DELAY_milliseconds(STCC4_SINGLE_SHOT_MS);
    if (!STCC4_readMeasurement(&co2, &temp_c, &humi_pct)) return false;

    // float → 固定小数点へ変換 (四捨五入)
    //  温度: ℃ * 100  (int16, 例 23.45℃ → 2345)
    out->temperature = (int16_t)(temp_c * 100.0f + (temp_c >= 0.0f ? 0.5f : -0.5f));
    //  湿度: % * 100   (uint16, 0..10000)。STCC4_readMeasurement 側で 0..100% に
    //        クランプ済みなので 0xFFFF(=空きレコード判定値) には決してならない。
    out->humidity    = (uint16_t)(humi_pct * 100.0f + 0.5f);
    //  CO2: ppm をそのまま
    out->co2_ppm     = co2;

    return true;
}

bool Measurement_Record(void)
{
    // 満杯停止: これ以上は古いデータを上書きしない
    if (rec_latest >= MAX_RECORD_COUNT) return false;

    SensorData_t rec;
    if (!Measurement_Sample(&rec)) return false;
    if (!W25_WriteRecord(rec_latest, &rec)) return false;

    rec_latest++;
    return true;
}
