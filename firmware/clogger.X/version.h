#ifndef VERSION_H
#define VERSION_H

#define FW_VERSION       "3.4.1"   // 手動更新のセマンティックバージョン(機能追加/リリース時に上げる)
#define PROTOCOL_VERSION 1         // JSONプロトコル世代(互換性が壊れる変更で上げる)

// ビルド日時 (コンパイル時に自動設定。「いつ時点のファームか」の判別用)。
// 例: __DATE__ = "Jul 30 2026", __TIME__ = "12:34:56"
// 注: json_protocol.c が再コンパイルされた時刻になるため、正確な刻印にはクリーンビルド推奨。
#define FW_BUILD_DATE    __DATE__
#define FW_BUILD_TIME    __TIME__

#endif /* VERSION_H */
