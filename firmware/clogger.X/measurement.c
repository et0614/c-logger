#include "measurement.h"
#include "stcc4.h"
#include "w25q64.h"
#include "i2c_master.h"
#include "crc.h"
#include "mcc_generated_files/timer/delay.h"

// STCC4 single-shot 計測の完了待ち時間 [ms] (datasheet: 約500ms)
#define STCC4_SINGLE_SHOT_MS  500

// 次に書き込むレコード index (= 有効レコード数)
uint32_t rec_latest = 0;

// single-shot をトリガ済みか (= 読み出し待ち)
static bool s_pending = false;

// STCC4を初期化済みか (USB通信モードでの live計測/セルフテスト用)。
static bool s_sensor_inited = false;

// CO2センサ(STCC4)を実装しているか。初期化時に I2C の応答で判定する。
// CO2なし版は STCC4 を実装せず、R7・R8(0Ω)で温湿度センサ(SHT40)をホストのI2Cに直結する。
static bool s_has_co2 = true;

// <editor-fold defaultstate="collapsed" desc="SHT4x (CO2なし版で直接読む温湿度センサ)">

#define SHT4X_ADDRESS     0x44  // SHT40-AD1B
#define SHT4X_CMD_MEASURE 0xFD  // 高精度で1回計測
#define SHT4X_MEASURE_MS  10    // 計測完了までの待ち時間 (datasheet max 8.3ms)

static bool SHT4X_isConnected(void)
{
    return I2C_IsConnected(SHT4X_ADDRESS);
}

static bool SHT4X_trigger(void)
{
    const uint8_t cmd = SHT4X_CMD_MEASURE;
    return I2C_Write(SHT4X_ADDRESS, &cmd, 1);
}

// 計測結果(温度・湿度 各2バイト+CRC)を読む。湿度は 0..100% にクランプする。
static bool SHT4X_read(float *temperature, float *humidity)
{
    uint8_t buf[6];
    if (!I2C_Read(SHT4X_ADDRESS, buf, sizeof(buf))) return false;
    if (CRC_calc8(&buf[0], 2) != buf[2] || CRC_calc8(&buf[3], 2) != buf[5]) return false;

    uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];
    *temperature = -45.0f + 175.0f * (float)rawT / 65535.0f;
    float h = -6.0f + 125.0f * (float)rawH / 65535.0f;
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;
    *humidity = h;
    return true;
}

// </editor-fold>

// 計測トリガ/読み出しを、CO2あり版(STCC4)とCO2なし版(SHT4x直結)で振り分ける。
// CO2なし版の co2 は CO2_NONE を返す。
static bool sensorTrigger(void)
{
    return s_has_co2 ? STCC4_measureSingleShot() : SHT4X_trigger();
}

static bool sensorRead(uint16_t *co2, float *temperature, float *humidity)
{
    if (s_has_co2) return STCC4_readMeasurement(co2, temperature, humidity);
    *co2 = CO2_NONE;
    return SHT4X_read(temperature, humidity);
}

// STCC4 をソフトリセットし、応答の有無で CO2あり/なし を判定する。
static void initSensor(void)
{
    // STCC4 ソフトリセット (general call) で既知の状態にする
    STCC4_initialize();
    DELAY_milliseconds(10);
    s_has_co2 = STCC4_isConnected();
    s_sensor_inited = true; // 初期化済みとして記録
}

void Measurement_Initialize(void)
{
    initSensor();

    s_pending = false; // 読み出し待ちをクリア (セッション開始時の整合)

    // Flash に保存済みのレコード数を復元 → 次の書き込み位置
    rec_latest = W25_Count_Record();
}

void Measurement_EnsureSensor(void)
{
    // まだ初期化していなければソフトリセットで idle にする。2回目以降は何もしない
    // (毎回ソフトリセットすると bypass位相で CO2 が 390ppm を返すため避ける)。
    if (!s_sensor_inited) initSensor();
}

bool Measurement_SensorConnected(void)
{
    return s_has_co2 ? STCC4_isConnected() : SHT4X_isConnected();
}

bool Measurement_HasCO2(void)
{
    return s_has_co2;
}

bool Measurement_LiveSample(SensorData_t *out)
{
    Measurement_EnsureSensor();
    return Measurement_Sample(out); // single-shot → 500ms待ち → 読み出し (Flash保存なし)
}

bool Measurement_StartConditioning(void)
{
    // CMD 送信のみ (即 return)。以降 約22秒はセンサが他コマンドを受け付けないため、
    // 呼び出し側が MEASUREMENT_CONDITIONING_SEC 秒待ってから計測を始めること。
    // Measurement_Initialize() (ソフトリセット=idle) の直後に呼ぶ前提。
    if (!s_has_co2) return false; // CO2なし版は不要
    return STCC4_performConditioning();
}

bool Measurement_FactoryReset(void)
{
    // まず general-call ソフトリセットで既知の idle 状態に戻す (sleep中や計測中でも確実に
    // コマンドを受け付けさせるため)。STCC4_initialize() 内で 10ms 待機する。
    STCC4_initialize();
    // 工場出荷リセット (CMD送信→90ms→応答0x0000を確認)。センサ内部の校正値等を初期化する。
    return STCC4_performFactoryReset();
}

bool Measurement_Sample(SensorData_t *out)
{
    uint16_t co2;
    float    temp_c;   // 温度 [℃]
    float    humi_pct; // 相対湿度 [%RH]

    // single-shot 計測をトリガ → 完了待ち → 結果読み出し
    if (!sensorTrigger()) return false;
    if (s_has_co2) DELAY_milliseconds(STCC4_SINGLE_SHOT_MS);
    else           DELAY_milliseconds(SHT4X_MEASURE_MS);
    if (!sensorRead(&co2, &temp_c, &humi_pct)) return false;

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
    if (!sensorTrigger()) return false; // I2Cコマンドのみ。即return
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
    if (sensorRead(&co2, &temp_c, &humi_pct))
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

// ====== CO2 5秒平均計測 ======
// 区間末尾の CO2_AVG_WINDOW_SEC 秒だけ連続 single-shot し、その平均を1レコードとして保存する。
// 温湿度は平均化せず、窓内の最後の有効サンプル値を使う。

static uint32_t s_co2_sum   = 0;   // 窓内 CO2 の合計
static uint16_t s_co2_cnt   = 0;   // 窓内 有効サンプル数
static int16_t  s_last_temp = 0;   // 窓内 最後の有効サンプルの温度 (×100)
static uint16_t s_last_hum  = 0;   // 窓内 最後の有効サンプルの湿度 (×100)

// 直前にトリガした single-shot の結果を読み、有効なら CO2 を積算し温湿度を保持する。
// 内部ヘルパ。pending でなければ何もしない。
static void avgReadPending(void)
{
    if (!s_pending) return;
    s_pending = false;

    uint16_t co2;
    float    temp_c, humi_pct;
    if (sensorRead(&co2, &temp_c, &humi_pct))
    {
        if (s_has_co2) s_co2_sum += co2;
        s_co2_cnt++;
        s_last_temp = (int16_t)(temp_c * 100.0f + (temp_c >= 0.0f ? 0.5f : -0.5f));
        s_last_hum  = (uint16_t)(humi_pct * 100.0f + 0.5f);
    }
    // 読み出し失敗は「そのサンプルを捨てる」だけ。窓内に1つでも有効値があれば平均は成立する。
}

void Measurement_AvgBegin(void)
{
    s_co2_sum = 0;
    s_co2_cnt = 0;
    Measurement_Trigger(); // 最初の single-shot をトリガ (s_pending=true)
}

void Measurement_AvgSample(void)
{
    avgReadPending();       // 前tickのトリガ結果を読んで積算
    Measurement_Trigger();  // 次の single-shot をトリガ
}

bool Measurement_AvgStore(void)
{
    if (rec_latest >= MAX_RECORD_COUNT) return false; // 満杯停止

    avgReadPending(); // 窓末尾のトリガ結果を読んで積算

    SensorData_t rec;
    if (s_co2_cnt > 0)
    {
        rec.temperature = s_last_temp;                                   // 温湿度は平均化せず最後の有効値
        rec.humidity    = s_last_hum;
        rec.co2_ppm     = s_has_co2 ? (uint16_t)((s_co2_sum + s_co2_cnt / 2) / s_co2_cnt) // 四捨五入平均
                                    : CO2_NONE;
    }
    else
    {
        // 窓内に有効サンプルなし → 欠測レコード(時刻とindexの対応を保つため index は進める)
        rec.temperature = 0;
        rec.humidity    = 0;
        rec.co2_ppm     = CO2_INVALID;
    }

    if (!W25_WriteRecord(rec_latest, &rec)) return false;
    rec_latest++;
    return true;
}
