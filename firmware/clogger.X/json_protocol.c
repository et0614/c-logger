#include "json_protocol.h"
#include "jsmn.h"
#include "usb_comm.h"   // USB_Comm_SendString
#include "version.h"    // FW_VERSION / PROTOCOL_VERSION
#include "main.h"       // getBatteryVoltage_mV / Clock_Set/Get / setLoggingActive
#include "eeprom_manager.h" // EM_mSettings / EM_saveMeasurementSetting
#include "measurement.h"    // Measurement_ClearData
#include "w25q64.h"         // W25_Count_Record (get_status)

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 設定
// ============================================================
#define JP_MAX_CMD     256   // 1コマンドの最大文字数 (ラインバッファ)
#define JP_MAX_TOKENS  32    // jsmn トークン上限 (現状のコマンドには十分)
#define JP_REPLY_CAP   128   // 1応答の最大文字数

// ============================================================
// 受信ラインバッファ
// ============================================================
static char     s_line[JP_MAX_CMD];
static uint16_t s_pos = 0;

// ============================================================
// 応答送信
// ============================================================
static void send_line(const char *s)
{
    USB_Comm_SendString(s);
}

// 成功(追加フィールド無し): {"id":N,"ok":true}\n
static void reply_ok(int32_t id)
{
    char b[48];
    snprintf(b, sizeof(b), "{\"id\":%ld,\"ok\":true}\n", (long)id);
    send_line(b);
}

// 失敗: {"id":N,"ok":false,"error":"<code>"}\n
static void reply_error(int32_t id, const char *code)
{
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b), "{\"id\":%ld,\"ok\":false,\"error\":\"%s\"}\n",
             (long)id, code);
    send_line(b);
}

// ============================================================
// jsmn トークン操作ヘルパ
// ============================================================

// 文字列トークン t が C 文字列 s と一致するか
static bool tok_eq(const char *json, const jsmntok_t *t, const char *s)
{
    int len = t->end - t->start;
    return t->type == JSMN_STRING
        && (int)strlen(s) == len
        && strncmp(json + t->start, s, (size_t)len) == 0;
}

// プリミティブ/文字列トークンを整数として解釈する
static int32_t tok_int(const char *json, const jsmntok_t *t)
{
    char tmp[12];
    int len = t->end - t->start;
    if (len <= 0 || len >= (int)sizeof(tmp)) return 0;
    memcpy(tmp, json + t->start, (size_t)len);
    tmp[len] = '\0';
    return (int32_t)atol(tmp);
}

// オブジェクト obj_idx の直下から key に対応する「値トークン」の index を返す。
// 見つからなければ -1。ネストした値 (object/array) も正しくスキップする。
static int obj_get(const char *json, const jsmntok_t *t, int ntok,
                   int obj_idx, const char *key)
{
    if (obj_idx < 0 || t[obj_idx].type != JSMN_OBJECT) return -1;

    int end = t[obj_idx].end;
    int i = obj_idx + 1;
    while (i < ntok && t[i].start < end)
    {
        // t[i] はキー、t[i+1] はその値
        if (tok_eq(json, &t[i], key)) return i + 1;

        // キーをスキップ → 値とその子孫をスキップ
        i++;                       // 値トークンへ
        if (i >= ntok) break;
        int vend = t[i].end;
        i++;                       // 値の最初の子へ
        while (i < ntok && t[i].start < vend) i++;
    }
    return -1;
}

// ============================================================
// コマンドハンドラ
//   id      : リクエスト id (応答に必要)
//   json    : 受信した完全な JSON 文字列
//   t       : jsmn パース結果
//   ntok    : 有効トークン数
//   params  : "params" 値トークンの index (無ければ -1)
// ============================================================
typedef void (*jp_handler_fn)(int32_t id, const char *json,
                              const jsmntok_t *t, int ntok, int params);

// 疎通確認: 追加フィールド無しで ok を返すだけ
static void ph_ping(int32_t id, const char *json,
                    const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    reply_ok(id);
}

// 機体固有IDを返す (読み取り専用, AVRシリコンシリアル由来の8桁hex)。ユーザー変更不可。
static void ph_get_id(int32_t id, const char *json,
                      const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b),
             "{\"id\":%ld,\"ok\":true,\"device_id\":\"%08lX\"}\n",
             (long)id, (unsigned long)Device_GetID());
    send_line(b);
}

// ファームウェア情報を返す
static void ph_hello(int32_t id, const char *json,
                     const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b),
             "{\"id\":%ld,\"ok\":true,\"device\":\"C-Logger\",\"fw\":\"%s\",\"proto\":%d,\"build\":\"%s %s\"}\n",
             (long)id, FW_VERSION, (int)PROTOCOL_VERSION, FW_BUILD_DATE, FW_BUILD_TIME);
    send_line(b);
}

// 電池電圧 [mV] を返す
static void ph_get_battery(int32_t id, const char *json,
                           const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    uint16_t mv = getBatteryVoltage_mV();
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b),
             "{\"id\":%ld,\"ok\":true,\"vbat_mV\":%u}\n",
             (long)id, (unsigned)mv);
    send_line(b);
}

// params.n をそのまま返すエコー (params 抽出パスの疎通確認用)
static void ph_echo(int32_t id, const char *json,
                    const jsmntok_t *t, int ntok, int params)
{
    int32_t n = 0;
    if (params >= 0)
    {
        int nt = obj_get(json, t, ntok, params, "n");
        if (nt >= 0) n = tok_int(json, &t[nt]);
    }
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b),
             "{\"id\":%ld,\"ok\":true,\"n\":%ld}\n",
             (long)id, (long)n);
    send_line(b);
}

// ロギング(計測)開始。応答後、メインループがロギングモードへ移行し、
// STANDBYスリープ＋定期計測に入る。省電力のため以降USBは応答しなくなる
// (再びUSB通信するにはデバイスをリセットしてUSB通信モードに戻す)。
static void ph_start_logging(int32_t id, const char *json,
                             const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    setLoggingActive(true);
    reply_ok(id);
    // この直後メインループがSTANDBYに入りUSBを処理しなくなるため、ackを確実に送出する。
    USB_Comm_Flush();
    // ack送出後にUSBを完全停止する。USB有効のままだとOSCHF(12MHz)が起き続けSTANDBYでも
    // ~2mA消費するため(実測)。以降USB送信不可。通信に戻すにはリセット。
    USB_Comm_Disable();
}

// ロギング(計測)停止。USB通信モードに留まる。
static void ph_stop_logging(int32_t id, const char *json,
                            const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    setLoggingActive(false);
    reply_ok(id);
}

// 日時設定: params.epoch (UNIX秒) をソフトウェア時計に設定する。
static void ph_set_time(int32_t id, const char *json,
                        const jsmntok_t *t, int ntok, int params)
{
    int et = (params >= 0) ? obj_get(json, t, ntok, params, "epoch") : -1;
    if (et < 0) { reply_error(id, "missing_epoch"); return; }
    Clock_Set((uint32_t)tok_int(json, &t[et]));
    reply_ok(id);
}

// 計測間隔設定: params.sec (秒) を interval_co2 に設定し EEPROM 保存する。
static void ph_set_interval(int32_t id, const char *json,
                            const jsmntok_t *t, int ntok, int params)
{
    int st = (params >= 0) ? obj_get(json, t, ntok, params, "sec") : -1;
    if (st < 0) { reply_error(id, "missing_sec"); return; }
    int32_t sec = tok_int(json, &t[st]);
    if (sec <= 0) { reply_error(id, "invalid_sec"); return; }
    EM_mSettings.interval_co2 = (unsigned int)sec;
    EM_saveMeasurementSetting();
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b), "{\"id\":%ld,\"ok\":true,\"interval\":%ld}\n",
             (long)id, (long)sec);
    send_line(b);
}

// メモリ初期化: 保存済みデータを消去し件数を 0 にする。
// (使用済みセクタの物理消去。データ量が多いと応答まで時間がかかる場合あり)
static void ph_clear_memory(int32_t id, const char *json,
                            const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    Measurement_ClearData();
    reply_ok(id);
}

// 工場出荷リセット (保守用): CO2センサ(STCC4)を工場出荷状態に戻す。
// 一般ユーザーは使わない。ロガーのFlashデータ/設定は消さず、センサ内部のみ初期化する。
// 約100msブロックしてからセンサの応答可否で ok/error を返す。
static void ph_factory_reset(int32_t id, const char *json,
                             const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    if (Measurement_FactoryReset()) reply_ok(id);
    else                            reply_error(id, "factory_reset_failed");
}

// 状態取得 (検証用): logging / interval / 保存件数 / 現在時刻 / 計測開始日時 を返す。
static void ph_get_status(int32_t id, const char *json,
                          const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    char b[192];
    snprintf(b, sizeof(b),
        "{\"id\":%ld,\"ok\":true,\"logging\":%s,\"interval\":%u,"
        "\"count\":%lu,\"capacity\":%lu,\"epoch\":%lu,\"start_dt\":%lu}\n",
        (long)id,
        getLoggingActive() ? "true" : "false",
        (unsigned)EM_mSettings.interval_co2,
        (unsigned long)W25_Count_Record(),
        (unsigned long)MAX_RECORD_COUNT,
        (unsigned long)Clock_Get(),
        (unsigned long)EM_mSettings.start_dt);
    send_line(b);
}

// ライブ計測 (Flash保存なし): STCC4 single-shot で co2/温湿度を1回取得して返す。
// USB接続のままセンサ確認/外気校正(FRC)確認に使う。内部で約500msブロックする。
static void ph_measure(int32_t id, const char *json,
                       const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;
    SensorData_t rec;
    if (!Measurement_LiveSample(&rec)) { reply_error(id, "measure_failed"); return; }
    int16_t tc = rec.temperature;                 // ℃*100
    int th = tc / 100, tl = tc % 100; if (tl < 0) tl = -tl;
    char co2s[8]; // CO2なし版は null
    if (rec.co2_ppm == CO2_NONE) snprintf(co2s, sizeof(co2s), "null");
    else                         snprintf(co2s, sizeof(co2s), "%u", (unsigned)rec.co2_ppm);
    char b[JP_REPLY_CAP];
    snprintf(b, sizeof(b),
        "{\"id\":%ld,\"ok\":true,\"co2_ppm\":%s,\"temp_C\":%d.%02d,\"hum_pct\":%u.%02u}\n",
        (long)id, co2s, th, tl,
        (unsigned)(rec.humidity / 100), (unsigned)(rec.humidity % 100));
    send_line(b);
}

// セルフテスト: フラッシュ(JEDEC ID + write/read/verify) / センサ(I2C ACK) / ライブ計測 を一括診断。
// センサは CO2あり版なら STCC4、CO2なし版なら SHT4x。"co2" で版を返す。
static void ph_selftest(int32_t id, const char *json,
                        const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;

    uint8_t fid[3] = { 0, 0, 0 };
    bool flash_id = W25_ReadJEDECID(fid);
    bool flash_rw = W25_SelfTest();
    Measurement_EnsureSensor(); // CO2あり/なしの判定を済ませる
    bool has_co2   = Measurement_HasCO2();
    bool sensor_ok = Measurement_SensorConnected();

    SensorData_t rec;
    bool meas_ok = Measurement_LiveSample(&rec);
    int th = 0, tl = 0; unsigned hh = 0, hl = 0, co2 = 0;
    if (meas_ok)
    {
        int16_t tc = rec.temperature;
        th = tc / 100; tl = tc % 100; if (tl < 0) tl = -tl;
        hh = rec.humidity / 100; hl = rec.humidity % 100; co2 = rec.co2_ppm;
    }
    bool all_ok = flash_id && flash_rw && sensor_ok && meas_ok;
    char co2s[8]; // CO2なし版は null
    if (!has_co2) snprintf(co2s, sizeof(co2s), "null");
    else          snprintf(co2s, sizeof(co2s), "%u", co2);

    char b[256];
    snprintf(b, sizeof(b),
        "{\"id\":%ld,\"ok\":%s,"
        "\"flash\":{\"ok\":%s,\"rw\":%s,\"jedec\":\"%02X%02X%02X\"},"
        "\"co2\":%s,\"sensor_i2c\":%s,"
        "\"sensor\":{\"ok\":%s,\"co2_ppm\":%s,\"temp_C\":%d.%02d,\"hum_pct\":%u.%02u}}\n",
        (long)id, all_ok ? "true" : "false",
        flash_id ? "true" : "false", flash_rw ? "true" : "false",
        fid[0], fid[1], fid[2],
        has_co2 ? "true" : "false", sensor_ok ? "true" : "false",
        meas_ok ? "true" : "false", co2s, th, tl, hh, hl);
    send_line(b);
}

// 1回の送出にまとめるレコード数 (6B*40=240B。CDC送信バッファ512B以内)。
// 1レコードずつ送ると転送回数が増えて遅いので、まとめて USB_Comm_SendBytes に渡す。
#define DUMP_BATCH_RECS  40

// レコード [start, start+count) を生バイナリでバッチ送出する (dump / dump_range 共通)。
static void dump_records_binary(uint32_t start, uint32_t count)
{
    uint8_t buf[DUMP_BATCH_RECS * sizeof(SensorData_t)];
    uint16_t fill = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        SensorData_t rec;
        if (!W25_ReadRecord(start + i, &rec)) break; // 読み出し失敗で打ち切り
        memcpy(&buf[fill], &rec, sizeof(rec));
        fill += sizeof(rec);
        if (fill >= sizeof(buf))
        {
            USB_Comm_SendBytes(buf, fill);
            fill = 0;
        }
    }
    if (fill > 0) USB_Comm_SendBytes(buf, fill); // 端数を送出
}

// データ読み出し (dump): フレーミングはJSON行、レコード本体は生バイナリで送る。
//   ヘッダ(JSON行): {"id":N,"ok":true,"dump":"begin","count":M,"rec_size":6,"start_dt":S,"interval":IV}
//   本体          : M個の SensorData_t を連続したバイナリで送出 (little-endian:
//                   int16 temp(℃*100), uint16 hum(%*100), uint16 co2(ppm))
//   フッタ(JSON行): {"id":N,"dump":"end"}
// 受信側はヘッダの count*rec_size バイトを「行ではなく固定長で」読むので、バイナリ中に
// '\n'/'{' が出ても誤認しない。時刻は start_dt + i*interval で復元する。件数はFlashから取得。
static void ph_dump(int32_t id, const char *json,
                    const jsmntok_t *t, int ntok, int params)
{
    (void)json; (void)t; (void)ntok; (void)params;

    uint32_t count = W25_Count_Record();
    char b[JP_REPLY_CAP];

    // ヘッダ (JSON 1行)
    snprintf(b, sizeof(b),
        "{\"id\":%ld,\"ok\":true,\"dump\":\"begin\",\"count\":%lu,\"rec_size\":%u,"
        "\"start_dt\":%lu,\"interval\":%u}\n",
        (long)id, (unsigned long)count, (unsigned)sizeof(SensorData_t),
        (unsigned long)EM_mSettings.start_dt, (unsigned)EM_mSettings.interval_co2);
    send_line(b);

    // レコード本体 (生バイナリ, まとめ送り)
    dump_records_binary(0, count);

    // フッタ (JSON 1行)
    snprintf(b, sizeof(b), "{\"id\":%ld,\"dump\":\"end\"}\n", (long)id);
    send_line(b);
    USB_Comm_Flush(); // フッタを確実に押し出す
}

// 範囲指定データ読み出し (dump_range): dump を「部分範囲」に限定したもの。
//   params: { "start": 先頭レコードindex, "count": 読み出す件数 }
// 用途: dump が途中で止まる問題の切り分け。PC側から小さなチャンク(例:64件)を
//       インターバルを空けて複数回要求することで、(a)分割すれば全件取れるか
//       (=連続転送/電源が原因) (b)特定の絶対indexで必ず止まるか(=フラッシュ/データが原因)
//       (c)停止点より後(例 start=400)を直接読めるか、を判定する。
//   ヘッダ(JSON行): {"id":N,"ok":true,"dump":"begin","start":S,"count":C,"total":T,
//                    "rec_size":6,"start_dt":..,"interval":..}
//     start=実際に読み出す先頭index, count=このレスポンスで送る件数(範囲クランプ後),
//     total=フラッシュ上の有効レコード総数。
//   本体          : C 個の SensorData_t を生バイナリで送出。
//   フッタ(JSON行): {"id":N,"dump":"end"}
static void ph_dump_range(int32_t id, const char *json,
                          const jsmntok_t *t, int ntok, int params)
{
    int32_t start = 0, want = 0;
    if (params >= 0)
    {
        int st = obj_get(json, t, ntok, params, "start");
        int ct = obj_get(json, t, ntok, params, "count");
        if (st >= 0) start = tok_int(json, &t[st]);
        if (ct >= 0) want  = tok_int(json, &t[ct]);
    }
    if (start < 0) start = 0;
    if (want  < 0) want  = 0;

    uint32_t total = W25_Count_Record();

    // 範囲を [0, total] にクランプ
    uint32_t s = (uint32_t)start;
    uint32_t c;
    if (s >= total)            c = 0;            // 範囲外: 0件
    else if ((uint32_t)want > (total - s)) c = total - s;
    else                       c = (uint32_t)want;

    char b[160];
    snprintf(b, sizeof(b),
        "{\"id\":%ld,\"ok\":true,\"dump\":\"begin\",\"start\":%lu,\"count\":%lu,"
        "\"total\":%lu,\"rec_size\":%u,\"start_dt\":%lu,\"interval\":%u}\n",
        (long)id, (unsigned long)s, (unsigned long)c, (unsigned long)total,
        (unsigned)sizeof(SensorData_t),
        (unsigned long)EM_mSettings.start_dt, (unsigned)EM_mSettings.interval_co2);
    send_line(b);

    dump_records_binary(s, c); // まとめ送り

    snprintf(b, sizeof(b), "{\"id\":%ld,\"dump\":\"end\"}\n", (long)id);
    send_line(b);
    USB_Comm_Flush();
}

// コマンドテーブル (NULL 終端)
typedef struct {
    const char    *name;
    jp_handler_fn  handler;
} jp_command_t;

static const jp_command_t s_commands[] = {
    { "ping",          ph_ping          },
    { "hello",         ph_hello         },
    { "get_id",        ph_get_id        },
    { "get_battery",   ph_get_battery   },
    { "echo",          ph_echo          },
    { "start_logging", ph_start_logging },
    { "stop_logging",  ph_stop_logging  },
    { "set_time",      ph_set_time      },
    { "set_interval",  ph_set_interval  },
    { "clear_memory",  ph_clear_memory  },
    { "factory_reset", ph_factory_reset },
    { "get_status",    ph_get_status    },
    { "measure",       ph_measure       },
    { "selftest",      ph_selftest      },
    { "dump",          ph_dump          },
    { "dump_range",    ph_dump_range    },
    { NULL,            NULL             }
};

// ============================================================
// ディスパッチ
// ============================================================
void JP_ProcessLine(const char *line)
{
    // JSON コマンドのみ受理 ('{' 始まり以外は無視)
    if (line[0] != '{') return;

    jsmn_parser parser;
    jsmn_init(&parser);
    jsmntok_t toks[JP_MAX_TOKENS];
    int n = jsmn_parse(&parser, line, strlen(line), toks, JP_MAX_TOKENS);

    if (n < 1 || toks[0].type != JSMN_OBJECT)
    {
        reply_error(0, "parse_error");
        return;
    }

    // id を抽出 (無ければ 0)
    int32_t id = 0;
    int id_tok = obj_get(line, toks, n, 0, "id");
    if (id_tok >= 0) id = tok_int(line, &toks[id_tok]);

    // command を抽出
    int cmd_tok = obj_get(line, toks, n, 0, "command");
    if (cmd_tok < 0 || toks[cmd_tok].type != JSMN_STRING)
    {
        reply_error(id, "missing_command");
        return;
    }

    // params (任意)。あればオブジェクトであること。
    int params_tok = obj_get(line, toks, n, 0, "params");
    if (params_tok >= 0 && toks[params_tok].type != JSMN_OBJECT)
    {
        reply_error(id, "invalid_params");
        return;
    }

    // テーブル検索してディスパッチ
    for (const jp_command_t *c = s_commands; c->name != NULL; c++)
    {
        if (tok_eq(line, &toks[cmd_tok], c->name))
        {
            c->handler(id, line, toks, n, params_tok);
            return;
        }
    }

    reply_error(id, "unknown_command");
}

// ============================================================
// ラインバッファ
// ============================================================
void JP_AppendChar(char c)
{
    if (c == '\r' || c == '\n')
    {
        if (s_pos > 0)
        {
            s_line[s_pos] = '\0';
            JP_ProcessLine(s_line);
            s_pos = 0;
        }
    }
    else if (s_pos < JP_MAX_CMD - 1)
    {
        s_line[s_pos++] = c;
    }
    else
    {
        // オーバーフロー: 壊れた行を捨ててリセット (次の行から再同期)
        s_pos = 0;
    }
}
