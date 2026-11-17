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
  index.html            製品の紹介ページ（GitHub Pages のトップ）
  app/index.html        ブラウザ版の操作ページ（Web Serial）
  manual.md             取扱説明書（GitHub Pages では manual.html になる）
  _config.yml           GitHub Pages (Jekyll) の設定
  images/               基板の外観 (3d_top/3d_bottom) とレイアウト (pcb_top/pcb_bottom)
hardware/
  BOM.xlsx              設計用部品表（JLCPCB 部品番号・CO2あり/なしの別）
  datasheet/            主要部品のデータシート（ファイル名=参照記号）
  pcb/                  Fusion 設計データ (clogger.f3z)、回路図 PDF
  fab/                  JLCPCB 入稿データ（ガーバー、BOM・CPL を CO2あり/なし版それぞれ）
  enclosure/            ケース（上蓋）: case.3dm (Rhino)、case.stp (発注用)
firmware/clogger.X/     MPLAB X プロジェクト（AVR64DU32, MCC Melody: clogger.mc3）
firmware/bootloader/    USB ブートローダ（euboot）。USB でのファームウェア更新と、量産用の1ファイル化 (build_production_hex.py)
software/               PCツール: clogger_gui.py (GUI)、clogger.py (コマンド版・通信ライブラリ)、build_exe.bat (exe化)
                        開発用: read.py / start.py / selftest.py / usb_test.py / clock_drift.py
```

## バージョン

- 基板: 3.1（シルク表記「Version 3.1」）。修正ごとに 3.1, 3.2 … と上げる。履歴は `hardware/fab/README.md`
- ファームウェア: 3.4.3（`hello` 応答の `fw`。`firmware/clogger.X/version.h`）

## 配布 (GitHub Releases)

`firmware/release/clogger.hex` に配布するファームウェアを置いてコミットし、`v1`, `v2` … のタグを push すると、
GitHub Actions が `CLogger_vX.Y.Z.zip`（CLogger.exe（avrdude 同梱）、ファームウェア、avrdude のライセンス、README）を作って Releases に登録する。
手順は [firmware/release/README.md](firmware/release/README.md)。手元で作るなら `python software/make_release.py --avrdude <avrdude のフォルダ>`。

## ブラウザ版の操作ページ

`docs/app/index.html` は、ブラウザから USB の仮想 COM ポートを直接開いて操作するページ（Web Serial）。インストール不要で、PCツールの GUI 版と同じことができる（機器情報、記録の開始、CSV の保存とデータの消去、ライブ表示、補正係数、CO2 の校正、時計の補正、自己診断）。表示は日本語と英語を切り替えられる。通信方式と CSV の形式は `software/clogger.py` と同じ。HTML 1枚で、外部のライブラリは使っていない。

- 動くブラウザ: パソコンの Chrome・Edge。Firefox・Safari・iPhone/iPad では動かない。
- https か localhost でしか動かない。公開は GitHub Pages（リポジトリの Settings → Pages で、Branch を `main`、フォルダを `/docs` にする）。`docs/` がそのままサイトになる:
  - `https://et0614.github.io/c-logger/` 製品の紹介（`index.html`）
  - `https://et0614.github.io/c-logger/app/` 操作ページ。ケースや説明書の QR コードはここに向ける
  - `https://et0614.github.io/c-logger/manual.html` 取扱説明書（`manual.md` を Jekyll がページにする）
- 手元で試すなら `python -m http.server 8765 --directory docs` の後に `http://localhost:8765` を開く（取扱説明書は Jekyll を通さないので、手元では表示されない）。
- 記録間隔を入れると、新品の電池での持ちの参考値を出す。README「消費電流の実測」の値から平均電流を計算し、1.5倍の余裕を掛けている（`batteryLifeSec`）。実測し直したら、ここの定数も直す。
- ファームウェアの更新はできない（ブートローダは HID で、avrdude に任せている。PCツールを使う）。
- 時計のずれの測定は、ページを前面に出したまま行う（裏に回すとブラウザがタイマーを間引く）。

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
- **ピン割り当て**: LED=PD5、ボタン=PD6、フラッシュ CS=PA1、VBAT=PF5 (AIN21)、UDTC=PD2（基板3.1 から。基板3.0 は PF2 で、ファームウェア 3.3.8 以降は使えない）、I2C=PA2/PA3、SPI=PA4/PA5/PA6、32kHz 水晶=PF0/PF1。未使用ピンは入力バッファを無効にする
- **水晶**: FC-135（CL=12.5pF）。負荷容量 C4・C5 は 2×(CL−浮遊容量 2〜5pF) から 18pF。水晶の個体差（±20ppm）は、RTC の水晶誤差補正（RTC.CALIB、1ppm 単位）で1台ずつ合わせる。補正値は EEPROM に保存し、起動時に設定する（`get_clock_calib` / `set_clock_calib`、PCツールの Clock タブ）

### 基板
- **SHT40 は熱源から離す**: USB 接続中は LDO とマイコンが発熱するので、基板の端に置き、周囲をスリットで切って熱を遮る
- **電池のマイナス接点**: 電池ホルダ (MY-2032-06) はプラス側の金具だけで、マイナスは基板の円形パッドで受ける。パッドの表面にはんだを盛らない（MPD の推奨。電池のニッケル外装との腐食）。電池の下に配線・ビア・スルーホールの足を置かない（レジストの盛り上がりで電池が浮き、こすれでレジストが剥げると短絡する）

## 開発メモ

- **USB**: AVR64DU のエラッタ 2.8.1（マルチパケット OUT 転送は偶数アドレス必須）を `usb_comm.c` で回避している（K-Logger と同じ）。
- **USB のクロック**: 内蔵発振器 OSCHF は USB の SOF に合わせて自動補正する（MCC の Clock Control で Auto-tune = USB SOF、`CLKCTRL_AUTOTUNE_SOF_gc`）。補正しないとフルスピード USB に必要な精度（±0.25%）を外れることがあり、チップの個体差で「記述子要求の失敗」「シリアル番号が読めない」「COM ポートは出るが通信できない」などが時々起きる（基板3.1 の試作で発生し、3.3.8 で解消）。ブートローダ（euboot）はまだ補正していない。
- **USB の既知の問題**（3.3.8 で確認）: (1) 抜き差しを短い間隔で繰り返すと、まれに COM ポートが開けるまで30秒ほどかかる（Windows 側の待ちの可能性）。(2) 接続したまま PC がロック・スリープすると、復帰後に本体が応答しなくなることがある。このとき USB を抜いても待機に戻らない（電池を外すと戻る）。
- **USB の安定化**（3.3.9。コードの分析に基づく対策で、上の2つが解消するかは実機で未確認）:
  - MCC の USB スタックは一度エラーを返すと以後の処理を止める（`usb_device.c` の `usbStatus`。未対応の要求が STALL にならずエラーになる経路や、EP0 の状態ずれがある）。メインループで検出し（`USB_Comm_Fault`）、detach して作り直す。
  - attach はメインループの直前で行う。attach からポーリング開始までが空くと、ホストのバスリセットと最初の要求が溜まり、処理の順が逆になる（要求に応答を用意した直後に古いリセットを処理し、`USB_Reset` が detach する）。LED の点滅中もポーリングを続ける。
  - `USB_Comm_Disable` で `USB0.ADDR` を消す。残すと、作り直した後の接続直後の Suspend を「抜かれた」と扱い、detach を繰り返す。
  - 抜けの確認は、Suspend・UDTC の立ち下がりに加えて「SOF が10秒来ない」でも行う（アドレスやスタックの状態によらない）。
  - USB 通信中だけウォッチドッグ（約8秒）を有効にする。待機と記録では使わない。
  - 調査用に `get_diag`（USB を作り直した回数と最後のエラーコード、ウォッチドッグの回数、抜けの確認・バスリセット・Suspend・Resume・クラス要求の回数。電池と USB の両方を外すまで残る）。`clogger.py info` で表示する。リセット要因（`rst`）はブートローダが先に消すので常に 0。
  - 3.3.9 でも、つないだままポートを開くのに30秒かかることがある（約175回に2回）。その間、本体はリセットも再接続もしていない。原因は未特定で、再現したら `get_diag` の回数の変化で切り分ける。
- **USB の製品名**（3.4.2）: 「C-Logger」にする（MCC の既定は「CDC Class Demo」。ブラウザの機器の選択画面に出る）。シリアル番号と同じく `usb_comm.c` で差し替えている。
- **USB のシリアル番号**（3.4.0）: 機体 ID（`get_id` と同じ8桁の16進）にする。MCC の既定は全台「1」で、Windows は同じ機器と見なすので2台を同時につなげなかった。生成コードは変えず、`usb_comm.c` に文字列記述子を持ち直している（`USB_Comm_SetSerial`）。シリアル番号が変わるので、3.4.0 にした後は Windows が新しい機器として認識し、COM ポート番号が機体ごとに変わる。
- **時計**（3.4.0）: 秒は PIT の割り込みの中で数える。メインループで数えると、データの読み出しや消去でループが1秒以上ふさがったときに取りこぼす。
- **データの消去**（3.4.0）: 使用済みの領域を末尾から先頭へ消す。件数は「先頭から連続した有効レコード」を二分探索で数えるので、先頭から消すと、途中で電源が落ちた後に消し残しを有効と数えてしまう。64KB ブロックがまるごと使用済みならブロック消去を使い（満杯で約3分 → 約40秒）、待ちの間も USB のポーリングを続ける（`W25_SetBusyHook`）。
- **電池切れの表示**（3.4.0）: 点滅の合間は STANDBY で眠る。USB・ADC を止め、フラッシュも Power-down にする。
- **CO2 センサの暖機**（3.4.1）: CO2あり版は、記録開始（コンディショニングの後）から1時間、記録間隔によらず毎秒 single shot を続ける（`CO2_WARMUP_SEC`）。STCC4 は3時間以上止めた後、精度が出るまでに連続計測で最大1時間、または single shot 360回かかる（datasheet 1.1.3）。窓の5回だけでは、記録間隔1分で72分、10分で12時間かかる。暖機中も記録は通常どおり（各区間の末尾5秒の平均）で、窓の外の結果は捨てる。暖機の消費は約1mA × 1時間。
- **CO2なし版の計測回数**（3.4.3）: CO2なし版は1区間に1回だけ測る（区間の最後の1秒）。CO2あり版は末尾5秒を毎秒測って CO2 を平均するが、温湿度は最後の1回しか使わないので、CO2なし版で5回測るのは無駄だった。
- **消費電流の実測**（CO2なし版、FW 3.4.2、クランプ電流計 5回巻き、誤差 ±10uA 程度）: 待機 約33uA、記録中 約38〜41uA。大半は昇圧 IC（TPS61021A）の自己消費（標準 17uA、出力側）と帰還抵抗（約5uA）で、ファームウェアでは減らせない。CR2032 を 200mAh と見て、CO2なし版の持ちは約7か月が上限。電池を入れたままの保管でも同じ速さで減る。数ミリ秒のパルスは電流計では正しく平均できないので、正確に測るには本体側に大きなコンデンサを入れて平滑する。
- **消費電流の実測**（CO2あり版、クランプ電流計 5回巻き）: 待機 約41uA、コンディショニング 約4.2mA × 約17秒、センサ idle 中 約95uA（待機 + 54uA。STCC4 の idle 55uA と合う）、毎秒計測中 平均 約1.03mA（計測中のピーク 約1.7mA が約0.58秒）。計測1回の電荷は約1.0mC（電池側）。1件につき5回測るので、平均電流は 約41 + 4900 ÷ 記録間隔[秒] uA。CR2032 を 200mAh と見た持ちの目安: 1分間隔 約2か月、2分 約3か月、5分 約5か月、10分 約5.5か月。5秒間隔以下は毎秒計測になり約8日。暖機（1時間）は記録開始ごとに約1.0mAh。sleep 中の電流は待機と同じと仮定していて、実測していない。
- **CO2 センサの sleep**（3.4.1）: 暖機の後は、計測の窓の合間に STCC4 を sleep にする（idle 55uA → 1uA。datasheet 3.4.6 の推奨手順）。待機に入るときも sleep にする（電源投入後は idle のまま待っている）。sleep 中の STCC4 はソフトリセットにも応答しないので、初期化の前に必ず起こす（起こさないと「CO2なし」と誤判定する）。
- **CO2 の出力の下限は 380 ppm**（STCC4 の出力範囲 380〜32000 ppm）。校正前で基準が低い個体は、外気でも 380 に張り付く。初回の通電では、連続計測12時間の後に外気に触れさせて自動校正（ASC）が働くまで精度が出ない（datasheet 1.1.4）。ASC は計測間隔 5〜600秒を前提にしているので、記録間隔が10分を超えると働かない可能性がある。
- **試験用の `debug_fill`**（3.4.1）: ダミーのレコードを書き足す（`{"count":N,"seed":S}`）。大量のデータでの読み出しと、ブロック消去の試験に使う。PCツールからは呼ばない。
- **USB の試験**: 書き込んだら PICkit を外してから試す（UPDI につないだままだと起動や USB が正常に動かない）。抜き差しは条件ごとに5回以上試して数える。
- **USB 接続の検出**: 電池で動いているときに USB を attach すると、D+ のプルアップ電圧が ESD 保護 (USBLC6) のダイオード経由で VBUS に回り込み、UDTC が High に見える。起動時に一度 detach してから UDTC を読み、USB が本当に接続されているときだけ USB を有効にする。
- **CO2あり/なしの判定**: 起動後の初回計測時に STCC4 (0x64) の応答で判定する。CO2なし版は R7・R8 (0Ω) で SHT40 (0x44) をホストの I2C に直結し、直接読む。記録の co2_ppm は 0xFFFE。同じ基板で、STCC4 か R7・R8 のどちらかを実装すれば両方の版になる（基板3.1 で両方確認済み）。
- **待機**: 電池のみで記録していないときは POWER-DOWN スリープ。フラッシュも Power-down にする（起動時に必ず復帰命令を送る）。起床はボタン (PD6) と USB 接続 (UDTC) のみ。USB を抜くと（エニュメレーション後の Suspend、UDTC の立ち下がり、または未エニュメレーションのまま10秒）、起動時と同じく detach してから UDTC を確かめ、外れていればリセットして待機に戻る。
- **記録中のフラッシュ**: 書き込むときだけ Power-down から起こし、書き込みが終わったら Power-down に戻す（待機電流 typ 10uA → 1uA）。
- **EEPROM**: 計測設定（記録間隔・記録開始日時）と校正値（温度・湿度・CO2 の補正係数 a・b、時計の補正値）の2ブロック。それぞれ CRC 付きで、壊れたブロックだけ既定値に戻す。並びを変えたら `eeprom_manager.c` の `EM_LAYOUT_VERSION` を上げる（起動時に全体を初期化し直す）。
- **測定値の補正**: フラッシュには生値を記録し、補正係数（補正後 = a × 生値 + b）は EEPROM に保存して PC ツールが読み出し時に適用する（係数を後から直しても過去のデータに使え、生値も残る）。CO2 はこのほか STCC4 の強制校正（FRC、`co2_frc`）も使える。FRC の前には single shot を30回以上（STCC4 の手順）。FRC の応答の補正量（返り値 − 32768）は、実際の変化と合わないことがある（380→約430 ppm で +1250）。換算はデータシートで要確認。
- **ADC**: 変換の完了は ADCBUSY ではなく RESRDY フラグで待つ（開始直後は BUSY がまだ立たず、前回の結果を読むことがある）。
