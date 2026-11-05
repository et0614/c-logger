# C-Logger — 温湿度・CO2 データロガー

温度・湿度（CO2あり版は CO2 濃度も）を一定間隔で内蔵フラッシュメモリに記録するデータロガー。
ボタン電池（CR2032）で動き、USB-C で PC につないでデータを読み出す。同じ基板で CO2あり版と CO2なし（温湿度のみ）版を作れる。

使い方は **[取扱説明書](docs/manual.md)** を参照。

![C-Logger 基板（3D）](docs/images/3d_top.png)

| 項目 | 内容 |
|---|---|
| MCU | AVR64DU32（ネイティブ USB） |
| センサ | SHT40（温湿度）、STCC4（CO2、CO2あり版のみ） |
| 記録媒体 | W25Q128 16MB（1件6バイト、約275万件） |
| 電源 | CR2032 ×1。USB 接続中は USB 給電（自動切替） |
| PC 通信 | USB-CDC（ドライバ不要）、Type-C |
| 基板 CAD | Fusion |

## ディレクトリ構成

```
docs/
  manual.md             取扱説明書
  images/               基板の外観 (3d_top/3d_bottom) とレイアウト (pcb_top/pcb_bottom)
hardware/
  BOM.xlsx              設計用部品表（JLCPCB 部品番号・単価・CO2あり/なしの別）
  datasheet/            主要部品のデータシート（ファイル名=参照記号）
  pcb/                  Fusion 設計データ (clogger.f3z)、回路図 PDF
  fab/                  JLCPCB 入稿データ（ガーバー、BOM・CPL を CO2あり/なし版それぞれ）
  enclosure/            ケース（上蓋）: case.3dm (Rhino)、case.stp (発注用)
  cost_estimate_next.xlsx  100台製造時の費用見積もり
firmware/clogger.X/     MPLAB X プロジェクト（AVR64DU32, MCC Melody: clogger.mc3）
firmware/bootloader/    USB ブートローダ（euboot）。USB でのファームウェア更新と、量産用の1ファイル化 (build_production_hex.py)
software/               PCツール: clogger_gui.py (GUI)、clogger.py (コマンド版・通信ライブラリ)、build_exe.bat (exe化)
                        開発用: read.py / start.py / selftest.py / usb_test.py / clock_drift.py
```

## バージョン

- 基板: 3.0（シルク表記「Version 3.0」）。修正ごとに 3.1, 3.2 … と上げる。履歴は `hardware/fab/README.md`
- ファームウェア: 3.2.0（`hello` 応答の `fw`。`firmware/clogger.X/version.h`）

## 配布 (GitHub Releases)

`firmware/release/clogger.hex` に配布するファームウェアを置いてコミットし、`v1`, `v2` … のタグを push すると、
GitHub Actions が `CLogger_vX.Y.Z.zip`（CLogger.exe（avrdude 同梱）、ファームウェア、avrdude のライセンス、README）を作って Releases に登録する。
手順は [firmware/release/README.md](firmware/release/README.md)。手元で作るなら `python software/make_release.py --avrdude <avrdude のフォルダ>`。

## PCツールの exe 化

`pip install pyserial pyinstaller` の後に `software/build_exe.bat` を実行すると `software/dist/CLogger.exe` ができる。
`software/` に `avrdude.exe` と `avrdude.conf`（avrdude 8.0 以降）を置いておくと exe に同梱され、Firmware タブでファームウェアを更新できる。

## ファームウェアの更新

フラッシュの先頭 2.5 KB に USB ブートローダ（euboot）を置き、ファームウェアは `0x0A00` 以降に置く。ボタンを押したまま起動するとブートローダに入り、PCツールの Firmware タブ（または `clogger.py update`、`avrdude`）で書き換える。詳細は [firmware/bootloader/README.md](firmware/bootloader/README.md)。

## 設計メモ

### 回路
- **電源**: 電池 (CR2032) → Pch MOSFET (Q1) → 昇圧 (TPS61021A, U6) → 3.3V。USB 接続中は VBUS → LDO (XC6206, U5) → ショットキー (D1) → 昇圧の入力に切り替わる。Q1 のゲートは VBUS（100k で GND へ）なので、USB 接続中は電池を切り離す。電池の逆接保護も兼ねる。K-Logger と同じ回路
- **帰還抵抗**: 470k / 150k、フィードフォワード 6.8pF（VOUT ≒ 3.29V。K-Logger と同じ）
- **C14 (100uF)**: ボタン電池は内部抵抗が大きく、フラッシュ書き込み（〜25mA）や STCC4（ピーク 4.2mA）の瞬間電流で電圧が落ちるため残している
- **USB 接続の検出**: VBUS を 22k / 47k で分圧（約 3.4V）して UDTC に入れる
- **USB**: Type-C（シェル足はスルーホールの長穴、PTH 指定）。CC1・CC2 は集合抵抗 RN1 を2個並列にして 5.0kΩ で GND へ。ESD 保護 USBLC6 互換 (U4)
- **CO2あり/なし版**: SHT40 は STCC4 のセンサ用 I2C（SDA_C/SCL_C）につながる。CO2なし版は STCC4 を載せず、R7・R8 (0Ω) で SDA/SCL を SDA_C/SCL_C に直結する。**STCC4 と R7・R8 を同時に実装しない**（STCC4 は SDA_C/SCL_C 側の I2C の親なので、親が2つになり通信できない）。SDA_C/SCL_C には STCC4 内蔵の 10k プルアップがある
- **SHT40 は必ず AD1B（I2C アドレス 0x44）**。STCC4 は 0x44 の SHT4x しか読まない
- **ピン割り当て**: LED=PD5、ボタン=PD6、フラッシュ CS=PA1、VBAT=PF5 (AIN21)、UDTC=PF2（基板3.0。次の版で PD2 に変更）、I2C=PA2/PA3、SPI=PA4/PA5/PA6、32kHz 水晶=PF0/PF1。未使用ピンは入力バッファを無効にする
- **水晶**: FC-135（CL=12.5pF）。負荷容量 C4・C5 は 2×(CL−浮遊容量 2〜5pF) から 18pF。水晶の個体差（±20ppm）は、RTC の水晶誤差補正（RTC.CALIB、1ppm 単位）で1台ずつ合わせる。補正値は EEPROM に保存し、起動時に設定する（`get_clock_calib` / `set_clock_calib`、PCツールの Clock タブ）

### 基板
- **SHT40 は熱源から離す**: USB 接続中は LDO とマイコンが発熱するので、基板の端に置き、周囲をスリットで切って熱を遮る
- **電池のマイナス接点**: 電池ホルダ (MY-2032-06) はプラス側の金具だけで、マイナスは基板の円形パッドで受ける。パッドの表面にはんだを盛らない（MPD の推奨。電池のニッケル外装との腐食）。電池の下に配線・ビア・スルーホールの足を置かない（レジストの盛り上がりで電池が浮き、こすれでレジストが剥げると短絡する）

## 開発メモ

- **USB**: AVR64DU のエラッタ 2.8.1（マルチパケット OUT 転送は偶数アドレス必須）を `usb_comm.c` で回避している（K-Logger と同じ）。
- **USB 接続の検出**: 電池で動いているときに USB を attach すると、D+ のプルアップ電圧が ESD 保護 (USBLC6) のダイオード経由で VBUS に回り込み、UDTC が High に見える。起動時に一度 detach してから UDTC を読み、USB が本当に接続されているときだけ USB を有効にする。
- **CO2あり/なしの判定**: 起動後の初回計測時に STCC4 (0x64) の応答で判定する。CO2なし版は R7・R8 (0Ω) で SHT40 (0x44) をホストの I2C に直結し、直接読む。記録の co2_ppm は 0xFFFE。
- **待機**: 電池のみで記録していないときは POWER-DOWN スリープ。フラッシュも Power-down にする（起動時に必ず復帰命令を送る）。起床はボタン (PD6) と USB 接続 (UDTC) のみ。USB を抜くと（エニュメレーション後の Suspend、UDTC の立ち下がり、または未エニュメレーションのまま10秒）、起動時と同じく detach してから UDTC を確かめ、外れていればリセットして待機に戻る。
- **記録中のフラッシュ**: 書き込むときだけ Power-down から起こし、書き込みが終わったら Power-down に戻す（待機電流 typ 10uA → 1uA）。
