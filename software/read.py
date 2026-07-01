#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
c-logger データ読み出しツール (一般ユーザー向け)

USB接続した c-logger の保存データを読み出して CSV に保存する。
計測中のデバイスは、リセットして USB通信モードに戻してから実行すること。

使い方:
    python read.py                 # clogger_data.csv に保存
    python read.py mydata.csv      # 指定ファイル名で保存
    python read.py -p COM3         # ポートを明示指定

CSV列: index, epoch, datetime, temp_C, hum_pct, co2_ppm
       (計測失敗(欠測)レコードは temp/hum/co2 が空欄)

依存: pip install pyserial
"""
import sys
import json
import time
import struct
import csv
import argparse
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial が必要です:  pip install pyserial")
    sys.exit(1)

# c-logger の USB 識別子 (firmware の usb_descriptors)
CLOGGER_VID = 0x04D8
CLOGGER_PID = 0x0B15

# 計測失敗(欠測)マーカー (firmware w25q64.h の CO2_INVALID と一致)
CO2_INVALID = 0xFFFF


def find_port():
    """c-logger(VID:PID=04D8:0B15) の COM ポートを自動検出する。無ければ None。"""
    for p in list_ports.comports():
        if p.vid == CLOGGER_VID and p.pid == CLOGGER_PID:
            return p.device
    return None


class Clogger:
    """c-logger からのデータ読み出し (dump)。"""

    def __init__(self, port, timeout=2.0):
        # USB-CDC なのでボーレートは無視されるが pyserial には必要。
        self.ser = serial.Serial(port, baudrate=115200,
                                 timeout=timeout, write_timeout=timeout)
        time.sleep(0.2)
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass
        self._id = 0

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def dump(self, save_csv):
        """保存データを取得し CSV に保存する。取得概要(dict)を返す。

        プロトコル:
          ヘッダ(JSON行) → レコード本体(固定長バイナリ count*rec_size) → フッタ(JSON行)
        本体は「行」ではなく「バイト数」で読むので、中に '\\n'/'{' が出ても誤認しない。
        各レコードの時刻は start_dt + index*interval で復元する。
        """
        self._id += 1
        self.ser.write((json.dumps({"id": self._id, "command": "dump"}) + "\n")
                       .encode("ascii"))

        # ヘッダ (JSON 1行)
        hdr = None
        for _ in range(5):
            line = self.ser.readline().decode("ascii", errors="replace").strip()
            if line.startswith("{"):
                hdr = json.loads(line)
                break
            if line == "":
                break
        if not hdr or hdr.get("dump") != "begin":
            raise RuntimeError(f"dumpヘッダの取得に失敗しました: {hdr}")

        count    = hdr.get("count", 0)
        rec_size = hdr.get("rec_size", 6)
        start_dt = hdr.get("start_dt", 0)
        interval = hdr.get("interval", 0)
        nbytes   = count * rec_size

        if count == 0:
            print("保存データがありません (0件)。")

        # 本体 (固定長バイナリ)
        raw = bytearray()
        idle = 0
        while len(raw) < nbytes:
            chunk = self.ser.read(nbytes - len(raw))
            if chunk:
                raw += chunk
                idle = 0
                print(f"\r受信中... {len(raw)}/{nbytes} バイト", end="", flush=True)
            else:
                idle += 1
                if idle >= 3:  # 約6秒(2s×3)無受信で打ち切り
                    break
        if nbytes > 0:
            print()
        self.ser.readline()  # フッタを読み捨て

        if len(raw) < nbytes:
            raise RuntimeError(f"受信が不足しました: {len(raw)}/{nbytes} バイト")

        # 復元 + CSV 出力 (little-endian: int16 temp, uint16 hum, uint16 co2[, uint16 vbat])
        # rec_size>=8 のレコードは末尾に vbat_mV(電池電圧[mV], デバッグ用)を持つ。
        has_vbat = rec_size >= 8
        rows = []
        n_invalid = 0
        n = len(raw) // rec_size
        with open(save_csv, "w", newline="") as f:
            w = csv.writer(f)
            header = ["index", "epoch", "datetime", "temp_C", "hum_pct", "co2_ppm"]
            if has_vbat:
                header.append("vbat_mV")
            w.writerow(header)
            for i in range(n):
                if has_vbat:
                    temp, hum, co2, vbat = struct.unpack_from("<hHHH", raw, i * rec_size)
                else:
                    temp, hum, co2 = struct.unpack_from("<hHH", raw, i * rec_size)
                    vbat = None
                ts = start_dt + i * interval
                tstr = datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")
                if co2 == CO2_INVALID:  # 欠測(計測失敗)
                    n_invalid += 1
                    row = [i, ts, tstr, "", "", ""]
                    rows.append((i, tstr, None, None, None, vbat))
                else:
                    row = [i, ts, tstr, f"{temp/100.0:.2f}", f"{hum/100.0:.2f}", co2]
                    rows.append((i, tstr, temp / 100.0, hum / 100.0, co2, vbat))
                if has_vbat:
                    row.append(vbat)
                w.writerow(row)
        return {"count": n, "invalid": n_invalid, "rows": rows}


def main():
    ap = argparse.ArgumentParser(
        description="c-logger の保存データを読み出して CSV 保存する")
    ap.add_argument("output", nargs="?", default="clogger_data.csv",
                    help="出力CSVファイル名 (既定 clogger_data.csv)")
    ap.add_argument("-p", "--port", default=None,
                    help="COMポート (省略時は自動検出)")
    args = ap.parse_args()

    port = args.port or find_port()
    if port is None:
        print("c-logger が見つかりません。USB接続を確認してください。")
        print("(計測中の場合はデバイスをリセットして USB通信モードに戻してください)")
        return 1

    try:
        dev = Clogger(port)
    except serial.SerialException as e:
        print(f"ポート {port} を開けません: {e}")
        print("他アプリがポートを使用していないか確認してください。")
        return 1

    try:
        print(f"接続: {port}")
        res = dev.dump(args.output)
        print(f"{res['count']} 件取得 (欠測 {res['invalid']} 件)")
        print(f"CSV保存: {args.output}")

        # 先頭/末尾の数件をプレビュー表示
        rows = res["rows"]

        def fmt(r):
            i, tstr, t, h, c, vb = r
            vbs = f"  {vb}mV" if vb is not None else ""
            if t is None:
                return f"  [{i}] {tstr}  (欠測){vbs}"
            return f"  [{i}] {tstr}  {t:.2f}C {h:.2f}% {c}ppm{vbs}"

        for r in rows[:3]:
            print(fmt(r))
        if len(rows) > 6:
            print("  ...")
            for r in rows[-3:]:
                print(fmt(r))
    except (RuntimeError, json.JSONDecodeError) as e:
        print(f"\nエラー: {e}")
        return 1
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
