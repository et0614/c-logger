#include "measurement.h"
#include "stcc4.h"
#include "w25q64.h"
#include "mcc_generated_files/timer/delay.h"

// STCC4 single-shot 計測の完了待ち時間 [ms] (datasheet: 約500ms)
#define STCC4_SINGLE_SHOT_MS  500

// 次に書き込むレコード index (= 有効レコード数)
uint32_t rec_latest = 0;

// single-shot をトリガ済みか (= 読み出し待ち)
static bool s_pending = false;

void Measurement_Initialize(void)
{
    // STCC4 ソフトリセット (general call) で既知の状態にする
    STCC4_initialize();
    DELAY_milliseconds(10);

    s_pending = false; // 読み出し待ちをクリア (セッション開始時の整合)

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

// ====== 非ブロッキング計測 ======

bool Measurement_Trigger(void)
{
    if (!STCC4_measureSingleShot()) return false; // I2Cコマンドのみ。即return
    s_pending = true;
    return true;
}

bool Measurement_IsPending(void)
{
    return s_pending;
}

void Measurement_ClearData(void)
{
    // 既存データの件数を調べ、使用済みセクタ(4KB単位)を消去する。
    // (全チップ消去は数十秒かかるため、使用分だけ消す)
    uint32_t n = W25_Count_Record();
    if (n > 0)
    {
        uint32_t lastAddr = W25_GetAddressFromRecordIndex(n - 1);
        for (uint32_t addr = DATA_START_ADDR; addr <= lastAddr; addr += 4096)
        {
            W25_SectorErase(addr);
        }
    }
    rec_latest = 0;
}

bool Measurement_ReadStore(void)
{
    if (!s_pending) return false;
    s_pending = false; // 読み出しは1回だけ

    if (rec_latest >= MAX_RECORD_COUNT) return false; // 満杯停止

    SensorData_t rec;
    uint16_t co2;
    float    temp_c, humi_pct;
    if (STCC4_readMeasurement(&co2, &temp_c, &humi_pct))
    {
        rec.temperature = (int16_t)(temp_c * 100.0f + (temp_c >= 0.0f ? 0.5f : -0.5f));
        rec.humidity    = (uint16_t)(humi_pct * 100.0f + 0.5f);
        rec.co2_ppm     = co2;
    }
    else
    {
        // 計測失敗: 欠測レコードを書いて index を進める(時刻とindexの対応を保つ)。
        rec.temperature = 0;
        rec.humidity    = 0;          // 0xFFFF(空き)以外 → 件数にカウントされる
        rec.co2_ppm     = CO2_INVALID; // 欠測マーカー
    }

    if (!W25_WriteRecord(rec_latest, &rec)) return false;
    rec_latest++;
    return true;
}
