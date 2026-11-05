# データシート

ファイル名は基板の参照記号（`hardware/pcb/clogger_schematic.pdf`）に合わせている。抵抗・コンデンサ・集合抵抗は省略。

| ファイル | 部品 | 型番 | LCSC |
|---|---|---|---|
| `U1.pdf` | マイコン | AVR64DU32 (Microchip) | C27283587 |
| `U2.pdf` | 温湿度センサ | SHT40-AD1B (Sensirion) | C2848306 |
| `U3.pdf` | フラッシュメモリ 128Mbit | W25Q128JVSIQ (Winbond) | C97521 |
| `U4.pdf` | USB ESD保護 | USBLC6-2SC6 互換 (TECH PUBLIC) | C2827654 |
| `U5.pdf` | USB用 LDO 3.3V | XC6206P332MR-G (Torex) | C5446 |
| `U6.pdf` | 昇圧 DC/DC 3.3V | TPS61021A (TI) | C193037 |
| `U7.pdf` | CO2センサ (CO2あり版のみ) | STCC4 (Sensirion) | C49191471 |
| `Q1.pdf` | 電池切り離し Pch MOSFET | AO3415A (AOS) | C133233 |
| `D1.pdf` | ショットキーダイオード | B5819W SL (JSCJ) | C8598 |
| `D2.pdf` | LED 赤 0603 | KT-0603R (Hubei KENTO) | C2286 |
| `L1.pdf` | 昇圧インダクタ 0.47uH | WIP252010S-R47ML (INPAQ) | C964116 |
| `Y1.pdf` | 32.768kHz 水晶 | Q13FC13500004 / FC-135 (Epson) | C32346 |
| `SW1.pdf` | タクトスイッチ | TS-1088-AR02016 (XUNPU) | C720477 |
| `J1.pdf` | USB Type-C レセプタクル | TYPE-C 16PIN 2MD(073) (SHOU HAN) | C2765186 |
| `BT1.pdf` | CR2032 電池ホルダ | MY-2032-06 (MYOUNG) | C964823 |

- L1 は JLC 実装では WPN252012HR47MT (Sunlord, C279796) を使う。LCSC で入手できないため、手はんだの試作では同寸・同値の WIP252010S-R47ML を使った。
