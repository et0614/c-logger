#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C-Logger PCツール (USB-CDC / JSONプロトコル)

USB接続した C-Logger の状態確認・記録開始・データ読み出しを行う。
ドライバ不要 (Windows 10以降は標準のUSBシリアルとして認識される)。

使い方:
    python clogger.py info                    機器情報・状態・電池電圧を表示
    python clogger.py start [-i 秒]           時刻を合わせて記録開始 (既存データは消去される)
    python clogger.py read [出力.csv]         保存データを CSV に保存
    python clogger.py measure [-n 回数]       温湿度(CO2)を連続表示 (保存しない, Ctrl+Cで終了)
    python clogger.py selftest                フラッシュ/センサの自己診断
    python clogger.py clear                   保存データの消去
    (GUI版: python clogger_gui.py)
    共通オプション: -p COM3 (ポート指定。省略時は自動検出)

記録中の機器はUSBに応答しない。読み出すときはUSBを挿し直すと自動でUSB通信モードに戻る
(記録はその時点で停止し、データは保存される)。ボタンの5秒長押しでも記録を止められる。

CSV列: index, epoch, datetime, temp_C, hum_pct, co2_ppm
  計測失敗(欠測)の記録は temp/hum/co2 が空欄。CO2なし版(温湿度のみ)は co2_ppm が空欄。

依存: pip install pyserial
"""
import sys
import json
import time
import csv
import struct
import argparse
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial が必要です:  pip install pyserial")
    sys.exit(1)

# USB識別子 (K-Logger と共通なので、接続後に hello の device 名で C-Logger か確認する)
USB_VID = 0x04D8
USB_PID = 0x0B15
DEVICE_NAME = "C-Logger"

# レコード形式 (firmware w25q64.h): int16 温度[℃×100], uint16 湿度[%×100], uint16 CO2[ppm]
REC_FORMAT = "<hHH"
REC_SIZE = struct.calcsize(REC_FORMAT)
CO2_INVALID = 0xFFFF   # 計測失敗(欠測)の記録
CO2_NONE = 0xFFFE      # CO2なし版の記録 (温湿度は有効)

DUMP_CHUNK = 10000  # dump_range 1回あたりのレコード数 (60KB)


def decode_record(temp, hum, co2):
    """生の値 → (temp_C, hum_pct, co2_ppm)。欠測は (None, None, None)、CO2なし版は co2_ppm=None。"""
    if co2 == CO2_INVALID:
        return None, None, None
    return temp / 100.0, hum / 100.0, None if co2 == CO2_NONE else co2


class CLogger:
    def __init__(self, port, timeout=2.0):
        # USB-CDC なのでボーレートは無視されるが pyserial には必要
        self.ser = serial.Serial(port, baudrate=115200, timeout=timeout, write_timeout=timeout)
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

    def _send(self, command, params=None):
        self._id += 1
        req = {"id": self._id, "command": command}
        if params is not None:
            req["params"] = params
        self.ser.write((json.dumps(req) + "\n").encode("ascii"))
        return self._id

    def _read_json_line(self, wait_s):
        """JSON行を1つ読む (wait_s 秒まで待つ)。取れなければ None。"""
        deadline = time.time() + wait_s
        while time.time() < deadline:
            line = self.ser.readline().decode("ascii", errors="replace").strip()
            if line.startswith("{"):
                try:
                    return json.loads(line)
                except json.JSONDecodeError:
                    return None
        return None

    def request(self, command, params=None, wait_s=3.0):
        """コマンドを送り応答(dict)を返す。応答が無い・エラー応答なら RuntimeError。"""
        rid = self._send(command, params)
        while True:
            j = self._read_json_line(wait_s)
            if j is None:
                raise RuntimeError(f"{command}: no response")
            if j.get("id") == rid:
                if not j.get("ok", False) and "error" in j:
                    raise RuntimeError(f"{command}: {j['error']}")
                return j

    def dump_range(self, start, count, progress=None):
        """レコード [start, start+count) を取得して (temp, hum, co2) の生値リストで返す。ヘッダも返す。"""
        rid = self._send("dump_range", {"start": start, "count": count})
        hdr = self._read_json_line(5.0)
        if not hdr or hdr.get("id") != rid or hdr.get("dump") != "begin":
            raise RuntimeError(f"dump: invalid header {hdr}")
        n = hdr.get("count", 0)
        size = hdr.get("rec_size", REC_SIZE)
        nbytes = n * size
        raw = bytearray()
        idle = 0
        while len(raw) < nbytes:
            chunk = self.ser.read(nbytes - len(raw))
            if chunk:
                raw += chunk
                idle = 0
                if progress:
                    progress(len(raw) // size)
            else:
                idle += 1
                if idle >= 3:
                    break
        self._read_json_line(3.0)  # フッタ
        if len(raw) < nbytes:
            raise RuntimeError(f"dump: incomplete data {len(raw)}/{nbytes} bytes")
        return hdr, [struct.unpack_from(REC_FORMAT, raw, i * size) for i in range(n)]

    def download(self, progress=None):
        """保存データを全件読み出す。
        @param progress progress(受信済み件数, 全件数) を随時呼ぶ
        @return (start_dt, interval, records[(temp, hum, co2)]) 。0件なら records は空。"""
        s = self.request("get_status")
        total = s["count"]
        if total == 0:
            return s.get("start_dt", 0), s["interval"], []
        recs = []
        hdr = None
        for start in range(0, total, DUMP_CHUNK):
            cnt = min(DUMP_CHUNK, total - start)
            cb = (lambda n, base=start: progress(base + n, total)) if progress else None
            hdr, part = self.dump_range(start, cnt, cb)
            recs += part
        return hdr["start_dt"], hdr["interval"], recs


def write_csv(path, start_dt, interval, recs):
    """CSV (index, epoch, datetime, temp_C, hum_pct, co2_ppm) を書く"""
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["index", "epoch", "datetime", "temp_C", "hum_pct", "co2_ppm"])
        for i, (temp, hum, co2) in enumerate(recs):
            ts = start_dt + i * interval
            t, h, c = decode_record(temp, hum, co2)
            w.writerow([i, ts, fmt_time(ts),
                        "" if t is None else f"{t:.2f}", "" if h is None else f"{h:.2f}",
                        "" if c is None else c])


def count_failed(recs):
    return sum(1 for r in recs if r[2] == CO2_INVALID)


def find_port():
    """C-Logger の COMポートを自動検出する (VID/PIDで候補を探し hello で確認)。"""
    for p in list_ports.comports():
        if p.vid == USB_VID and p.pid == USB_PID:
            try:
                dev = CLogger(p.device, timeout=1.0)
                try:
                    h = dev.request("hello")
                    if h.get("device") == DEVICE_NAME:
                        return p.device
                finally:
                    dev.close()
            except (serial.SerialException, RuntimeError):
                pass
    return None


def fmt_time(epoch):
    return datetime.fromtimestamp(epoch).strftime("%Y-%m-%d %H:%M:%S")


def default_csv_name(device_id, start_dt):
    return f"clogger_{device_id}_{datetime.fromtimestamp(start_dt).strftime('%Y%m%d_%H%M%S')}.csv"


def fmt_measure(r):
    """measure 応答 → 表示用文字列"""
    s = f"{r['temp_C']:6.2f} °C  {r['hum_pct']:6.2f} %RH"
    if r.get("co2_ppm") is not None:
        s += f"  {r['co2_ppm']:5d} ppm"
    return s


# ============================================================
# サブコマンド
# ============================================================

def cmd_info(dev, args):
    h = dev.request("hello")
    print(f"{h.get('device')}  FW {h.get('fw')}  proto {h.get('proto')}  build {h.get('build')}")
    print(f"機体ID: {dev.request('get_id').get('device_id')}")
    s = dev.request("get_status")
    print(f"記録中: {'はい' if s['logging'] else 'いいえ'}  記録間隔: {s['interval']}秒")
    days = s["count"] * s["interval"] / 86400
    print(f"保存件数: {s['count']:,} / {s['capacity']:,}  (約{days:.1f}日分)")
    if s["count"] > 0:
        print(f"記録開始: {fmt_time(s['start_dt'])}")
    print(f"機器の時計: {fmt_time(s['epoch'])}")
    b = dev.request("get_battery")
    print(f"電池電圧: {b['vbat_mV']/1000:.3f} V")


def cmd_start(dev, args):
    print("既存データを消去しています (データ量により数分かかります)...")
    dev.request("clear_memory", wait_s=300.0)
    if args.interval:
        dev.request("set_interval", {"sec": args.interval})
    now = int(time.time())
    dev.request("set_time", {"epoch": now})
    print(f"時刻設定: {fmt_time(now)}")
    dev.request("start_logging")
    print("記録を開始しました。USBを抜いても記録を続けます。")


def cmd_read(dev, args):
    def progress(n, total):
        print(f"\r受信中... {n:,}/{total:,}", end="", flush=True)
    start_dt, interval, recs = dev.download(progress)
    if not recs:
        print("保存データがありません (0件)。")
        return
    print()
    out = args.output or default_csv_name(dev.request("get_id").get("device_id", ""), start_dt)
    write_csv(out, start_dt, interval, recs)
    print(f"CSV保存: {out}")
    print(f"{len(recs):,} 件 (計測失敗 {count_failed(recs)} 件)  {fmt_time(start_dt)} 〜 "
          f"{fmt_time(start_dt + (len(recs) - 1) * interval)}")


def cmd_measure(dev, args):
    print("温湿度(CO2)を連続表示します (保存しません)。Ctrl+C で終了。")
    i = 0
    try:
        while args.count == 0 or i < args.count:
            print(fmt_measure(dev.request("measure")))
            i += 1
            time.sleep(args.wait)
    except KeyboardInterrupt:
        print()


def cmd_selftest(dev, args):
    r = dev.request("selftest", wait_s=10.0)
    ok = lambda b: "OK" if b else "NG"  # noqa: E731
    fl, se = r["flash"], r["sensor"]
    print(f"フラッシュ   : {ok(fl['ok'] and fl['rw'])}  (JEDEC {fl['jedec']}, 読み書き {ok(fl['rw'])})")
    print(f"版           : {'CO2あり' if r.get('co2') else 'CO2なし (温湿度のみ)'}")
    print(f"センサ       : {ok(r.get('sensor_i2c'))}")
    print(f"測定         : {ok(se['ok'])}  ({fmt_measure(se) if se['ok'] else '-'})")
    print(f"総合         : {ok(r['ok'])}")


def cmd_clear(dev, args):
    print("保存データを消去します (データ量により数分かかります)...")
    dev.request("clear_memory", wait_s=300.0)
    print("消去しました。")


def main():
    ap = argparse.ArgumentParser(description="C-Logger PCツール")
    ap.add_argument("-p", "--port", help="COMポート (省略時は自動検出)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="機器情報・状態を表示")
    p = sub.add_parser("start", help="時刻を合わせて記録開始 (既存データは消去)")
    p.add_argument("-i", "--interval", type=int, help="記録間隔 [秒]")
    p = sub.add_parser("read", help="保存データを CSV に保存")
    p.add_argument("output", nargs="?", help="出力CSVファイル名 (省略時は自動命名)")
    p = sub.add_parser("measure", help="温湿度(CO2)を連続表示 (保存しない)")
    p.add_argument("-n", "--count", type=int, default=0, help="回数 (0=無限)")
    p.add_argument("-w", "--wait", type=float, default=1.0, help="測定間隔 [秒]")
    sub.add_parser("selftest", help="自己診断")
    sub.add_parser("clear", help="保存データの消去")
    args = ap.parse_args()

    port = args.port or find_port()
    if port is None:
        print("C-Logger が見つかりません。USB接続を確認してください。")
        return 1
    try:
        dev = CLogger(port)
    except serial.SerialException as e:
        print(f"ポート {port} を開けません: {e}")
        return 1

    handlers = {"info": cmd_info, "start": cmd_start, "read": cmd_read,
                "measure": cmd_measure, "selftest": cmd_selftest, "clear": cmd_clear}
    try:
        handlers[args.cmd](dev, args)
    except RuntimeError as e:
        print(f"エラー: {e}")
        return 1
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
