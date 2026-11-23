# C-Logger 基板 3.2 製造データ（JLCPCB入稿用）

基板バージョン: **3.2**（シルク表記「Version 3.2」）。温湿度＋加速度。CO2 センサは載らない（CO2あり版は基板 3.1 で作る）。

| ファイル | 内容 |
|---|---|
| `JLCPCB_BOM_v3.2.xlsx` | 入稿用 BOM（列: Comment / Designator / Footprint / JLCPCB Part #） |
| `JLCPCB_CPL_v3.2.xlsx` | 入稿用の部品配置データ（列: Designator / Mid X / Mid Y / Layer / Rotation）。Fusion の部品配置出力（`PnP_C-Logger_v3.2_front.txt` / `back.txt`）から、UPDI のテストパッドを除いて作った。BOM と参照記号が全数一致 |
| `clogger_v3.2_gerber.zip` | ガーバー一式（Fusion Electronics 出力、2層、26 × 26 mm、ドリル `.XLN` とガーバージョブを含む） |

- 設計データ: `hardware/v3.2/pcb/clogger_v3.2.f3z`、回路図: `hardware/v3.2/pcb/clogger_v3.2_schematic.pdf`、外観: `hardware/v3.2/pcb/clogger_v3.2_top.png` / `_bottom.png`
- BOM は 3.1 の CO2なし版の BOM から作り、基板データ（f3z の中の .brd）の部品39個と参照記号・パッケージが全数一致することを確かめた。UPDI のテストパッド（UDPI_PAD）は実装部品ではないので除く

## 3.1 からの変更

| 部品 | 3.1 | 3.2 |
|---|---|---|
| U7（STCC4, CO2センサ） | CO2あり版で実装 | **削除** |
| R7・R8（0Ω、温湿度センサを I2C へ直結） | CO2なし版で実装 | **削除**（SHT40 はマイコンの I2C に直結） |
| U8（SC7A20HTR, 3軸加速度計、LCSC C19274408） | なし | **追加**。I2C アドレス 0x19（SDO は未接続）。INT1 → マイコンの PD1（11番）。INT2・CS・NC は未接続 |
| C16・C17（100nF） | なし | **追加**。U8 の VDD と VDDIO |
| U2（SHT40）・C6 | — | 位置を移動（U2: 22.1, 1.8 mm、C6: 23.9, 1.8 mm 90°） |

- U8 のフットプリントと 3D モデルは `hardware/lib/SC7A20H.lbr`・`SC7A20H.stp`（士蘭微電子のデータシート 版本 0.7 による）
- 回路図の記号ではマイコンの 10番が「VDDIO2」、6〜9番が「PC0〜PC3」になっているが、AVR64DU32 では 6=VUSB、7=DM、8=DP、9=PC3、10=PD0。番号どおりの配線なので動作には影響しない

## 発注時の指定

3.1 と同じ（`hardware/v3.1/fab/README.md` の「発注時の指定」）: 表面処理 ENIG、板厚 1.2 mm、J1 の長穴は PTH、U1 は Global Sourcing、BT1 は裏面のスルーホール部品。JLC のプレビューで **U8 の向きと1番ピンの位置**も確かめる（部品面から見て左上が1番）。

## 確認待ち

- C4・C5（18pF）は C1549（0402CG180J500NT）。基板データの LCSC 属性は C1555 になっているが誤りで、BOM は C1549 にしている（Fusion の属性も直すとよい）
- J1（USB-C）の位置は部品の原点 **13.0, 2.9 mm** にしている。Fusion の部品配置出力は、パッド全体（表面実装の信号ピンとシェル足のスルーホール）の外接枠の中心を出すので、Y が 4.20 mm になる。CPL はこれを原点の値に直した（3.1 の CPL も同じく 2.9 mm に直した）。JLC のプレビューで USB コネクタが基板の端の位置に合っているか確かめる
