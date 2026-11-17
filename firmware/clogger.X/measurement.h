/*
 * File:   measurement.h
 * Author: e.togashi
 *
 * 温湿度・CO2 の計測処理。STCC4 (CO2センサ) の single-shot 計測で CO2・温度・湿度を
 * まとめて取得し (温湿度は STCC4 が AHT4 を介して返す)、SensorData_t に変換して
 * 内蔵 Flash (W25Q128) に1レコードずつ保存する。
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

/// 記録中の省電力: on でフラッシュを Power-down にし、以降はレコードを書く間だけ起こす。
/// off でフラッシュを起こす (USB通信モードで読み出す前)。待機電流 typ 10uA → 1uA。
void Measurement_SetFlashSleep(bool on);

/// センサ初期化 + rec_latest を Flash から復元する。
/// SYSTEM_Initialize() / SPI0_Open() の後に1回呼ぶこと。
void Measurement_Initialize(void);

/// 長期未通電からの復帰用コンディショニングを開始する (STCC4 datasheet 推奨)。
/// コマンド送信のみで即 return する。送信後 約22秒はセンサが他コマンドを受け付けない
/// ので、呼び出し側で MEASUREMENT_CONDITIONING_SEC 秒待ってから計測を始めること。
/// @return true:コマンド送信成功
bool Measurement_StartConditioning(void);

/// コンディショニング完了までの待ち秒数 (datasheet 22秒 + 余裕)。
#define MEASUREMENT_CONDITIONING_SEC  24

/// CO2 平均化の窓 [秒]。各区間の「末尾この秒数」を連続計測し、その CO2 平均を1レコードとして記録する。
/// 例: 60秒間隔なら 55〜60秒の5サンプルを平均。区間が窓より短い場合は区間全体で平均する(実効窓=min(窓,間隔))。
/// 温湿度は平均化せず、窓内の最後の有効サンプル値を用いる。将来変更する可能性があるため定数で持つ。
/// CO2なし版は窓を使わず、区間の最後に1回だけ測る (main.c)。
#define CO2_AVG_WINDOW_SEC  5

/// CO2 センサの暖機 [秒] (CO2あり版のみ)。記録開始からこの間は、記録間隔によらず毎秒計測を続ける。
/// STCC4 は 3時間以上止めた後、精度が出るまでに連続計測で最大1時間 (または single shot 360回) かかる
/// (datasheet 1.1.3 Warm-Up)。窓の5回だけだと、記録間隔1分で72分、10分で12時間かかってしまう。
/// 暖機中も記録は通常どおり (各区間の末尾の窓の平均) で、窓の外の計測結果は読み捨てる。
#define CO2_WARMUP_SEC  3600

/// 暖機中の1回分: 前回の結果を読み捨て、次の single-shot をトリガする (毎秒呼ぶ)。
void Measurement_WarmupShot(void);

/// CO2 センサを sleep にする (idle 55uA → 1uA)。次のトリガで自動的に起こす。CO2なし版では何もしない。
void Measurement_SensorSleep(void);

/// 待機に入る前に CO2 センサを sleep にする (CO2あり/なしの判定前でも呼べる)。
void Measurement_SensorPowerDown(void);

/// CO2センサ(STCC4)を工場出荷状態にリセットする (センサ内部の校正値等が初期化される)。
/// ソフトリセットで idle にしてから実行する。約100msブロックする。
/// 注意: これはセンサのリセットであり、ロガーのFlashデータ/EEPROM設定は消さない。
/// 通常運用では不要で、長期保管後やセンサ異常時の保守用。
/// @return true:成功(センサが正常応答), false:I2C/CRC/応答エラー
bool Measurement_FactoryReset(void);

/// CO2 の強制校正 (FRC) の結果
typedef enum {
    FRC_OK,          // 成功
    FRC_NO_CO2,      // CO2なし版
    FRC_NOT_READY,   // 直前の計測が 30 回未満 (STCC4 の手順)
    FRC_FAILED       // 通信エラー、またはセンサが失敗を返した
} FrcResult;

/// FRC に必要な、直前の連続計測の回数 (STCC4 datasheet: single shot 30 回以上)
#define FRC_MIN_SAMPLES  30

/// CO2 の強制校正 (FRC): センサの出力を基準の CO2 濃度 ppm に合わせる (センサ内部に保存される)。
/// 安定した環境で Measurement_LiveSample を FRC_MIN_SAMPLES 回以上続けた直後に呼ぶこと。
/// @param correction センサが適用した補正量 [ppm]
FrcResult Measurement_ForcedRecalibration(uint16_t ppm, int16_t *correction);

/// STCC4を(まだなら)初期化する。USB通信モードでは boot時に初期化していないため、
/// live計測/セルフテストの前に呼ぶ。2回目以降は何もしない(ソフトリセットのbypass位相を避ける)。
void Measurement_EnsureSensor(void);

/// 計測に使うセンサ(CO2あり版は STCC4、CO2なし版は SHT4x)がI2Cバス上に存在するか (ACK確認)。セルフテスト用。
bool Measurement_SensorConnected(void);

/// CO2センサ(STCC4)を実装しているか (初期化時に判定)。false なら CO2なし版で、co2_ppm は CO2_NONE。
bool Measurement_HasCO2(void);

/// ライブ計測 (Flashに保存しない): STCC4を(必要なら)初期化し single-shot で1回計測して out に詰める。
/// USB接続のままセンサ動作確認/外気校正確認に使う。内部で約500msブロックする。
/// @return true:成功, false:通信/CRCエラー
bool Measurement_LiveSample(SensorData_t *out);

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

// ====== CO2 5秒平均計測 (区間末尾の連続計測 → 平均を1レコードに) ======
// 使い方(PIT 1秒周期): 窓開始tickで Begin → 以降の中間tickで Sample を毎tick →
// 区間境界tickで Store。各tickは前tickのトリガ分を読み出し、次の single-shot をトリガする
// パイプライン構成なので、500ms待ちは常にスリープ中に消化される。

/// CO2平均化の窓を開始する: アキュムレータをクリアし最初の single-shot をトリガする(即 return)。
void Measurement_AvgBegin(void);

/// 窓の中間tick: 前tickのトリガ結果を読んで CO2 を積算し(温湿度は最後の値を保持)、
/// 次の single-shot をトリガする(即 return)。
void Measurement_AvgSample(void);

/// 窓末尾(区間境界): 最後のトリガ結果を読んで積算し、CO2 平均を計算して Flash に保存する。
/// 温湿度は窓内最後の有効サンプル値を使う。窓内に有効サンプルが無ければ欠測レコードを書く。
/// @return true:保存成功, false:Flash満杯 or 書込み失敗
bool Measurement_AvgStore(void);

/// 保存済みデータを消去する (メモリ初期化)。使用済みの領域を物理消去し、
/// rec_latest を 0 に戻す。空き判定(humidity==0xFFFF)が正しく働くよう物理消去する。
/// 末尾から先頭へ消す (途中で電源が落ちても、残るのは先頭から連続した有効なデータになる)。
/// 注意: 消去はブロッキング (64KB ブロックあたり typ 150ms。満杯で typ 約40秒)。
void Measurement_ClearData(void);

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_H */
