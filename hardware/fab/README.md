# C-Logger 基板 製造データ（JLCPCB入稿用）

基板バージョン: **3.0**（シルク表記「Version 3.0」）

リポジトリには最新版のみを置く。以前の版は Git の履歴（タグ）で参照する。基板を修正して再発注するときは 3.1, 3.2 … と上げ、シルク表記と下記を更新する。

| ファイル | 内容 |
|---|---|
| `clogger_gerber.zip` | ガーバー一式（Fusion Electronics 出力、2層、26 × 26 mm） |
| `JLCPCB_BOM_CO2.xlsx` / `JLCPCB_CPL_CO2.xlsx` | CO2あり版の入稿用BOM・部品配置データ（U7 実装、R7・R8 なし） |
| `JLCPCB_BOM_noCO2.xlsx` / `JLCPCB_CPL_noCO2.xlsx` | CO2なし版（温湿度のみ）の入稿用BOM・部品配置データ（U7 なし、R7・R8 に 0Ω） |

- BOM の列: Comment / Designator / Footprint / JLCPCB Part #。CPL の列: Designator / Mid X / Mid Y / Layer / Rotation
- BOM・CPL は Fusion の部品配置出力（CAM の Assembly: `PnP_C-Logger_front.txt` / `back.txt`）と `hardware/BOM_next_draft.xlsx`（部品番号）から作成。UPDIテストパッドは実装部品ではないため除外。参照記号とパッケージは配置出力と全数一致を確認済み
- 設計データ: `hardware/pcb/clogger.f3z`、回路図: `hardware/pcb/clogger_schematic.pdf`、外観: `docs/images/`、ケース（蓋）: `hardware/enclosure/case.stp`

## CO2あり版・なし版
同じ基板で、実装する部品だけを変える。

| 部品 | CO2あり | CO2なし |
|---|---|---|
| U7（STCC4, CO2センサ） | 実装 | なし |
| R7・R8（0Ω, SDA/SCL を温湿度センサへ直結） | **なし** | 実装 |

- CO2あり版で R7・R8 を実装すると I2C の親が2つになり通信できなくなる。必ずどちらか一方の組み合わせで発注する
- 1回の注文では1種類しか指定できないので、両方作るときは注文を分ける

## 発注時の指定
- 板厚 **1.2 mm**（ガーバージョブファイルは 1.22 mm になっているので発注画面で指定する）
- USB-C（J1）のシェル足用長穴は外形レイヤ（GKO）のミリングで定義。**めっきスルーホール（PTH）として加工**されるよう備考で指定し、プレビューで確認する
- AVR64DU32（U1）は JLC在庫なし → Global Sourcing
- BT1（CR2032 電池ホルダ C964823）は**裏面のスルーホール部品**（Layer=Bottom）。JLCで実装するとスルーホール実装の費用がかかる。手はんだにする場合は BOM と CPL の BT1 行を削除
- 部品の向き（Rotation）は Fusion の値のまま。**JLCのプレビューで、特にU1〜U7・Q1・D1・D2・J1・X1・SW1の向きと1番ピン位置を確認**し、ずれていれば発注画面で補正する

## 版の履歴
| 版 | 日付 | 変更内容 |
|---|---|---|
| 3.0 | 2026-10-23 | 全面改版（旧 CO2-Logger 基板から）。USB を表面実装 micro-B からシェル足スルーホールの USB-C に変更し USB給電に対応、電池を CR2032（スルーホールのホルダ）に、フラッシュを W25Q128（SOIC-8）に、K-Logger と部品を共通化して JLC Basic 部品中心に、CO2なし版（R7・R8 の 0Ω で切替）に対応、裏面シルクにブランドロゴ「Popolo」・製品名・版表記を追加。変更の詳細は `hardware/next_revision_changes.md` |

- ロゴの元データ: popolo3 リポジトリの `assets/logo/`

## 未対応
- [ ] 回路図の Value が未設定（コンデンサ・抵抗が「CAPACITOR0402」等、RN=EXB-28V103JX 表記）。Value修正後に回路図PDFを再出力する
