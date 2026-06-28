#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
c-logger USB-CDC 通信テスト

PC(Windows) と c-logger 間の USB-CDC(仮想COMポート) 通信を確認する簡易ツール。
firmware の JSON コマンド (1行=1コマンド, '\\n' 終端) を送って応答を確認する。

依存:  pip install pyserial

使い方:
    python usb_test.py            # c-logger(VID:PID=04D8:0B15)を自動検出、無ければ一覧選択
    python usb_test.py COM7       # ポートを直接指定

注意:
    - USB-CDC なのでボーレートは無視される(任意の値でOK)。
    - "start_logging" を送るとデバイスはロギングモードへ移行しUSBが切れる。テストでは送らない。
"""

import sys
import json
import time
import struct
import csv
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial が必要です:  pip install pyserial")
    sys.exit(1)

# c-logger の USB 識別子 (usb_descriptors.c)
CLOGGER_VID = 0x04D8   # Microchip
CLOGGER_PID = 0x0B15


def pick_port(arg):
    """引数があればそれを使う。無ければ c-logger を自動検出、無ければ一覧から選択。"""
    if arg:
        return arg

    ports = list(list_ports.comports())
    if not ports:
        print("シリアルポートが見つかりません。USB接続とドライバを確認してください。")
        sys.exit(1)

    print("利用可能なポート:")
    match = None
    for i, p in enumerate(ports):
        vidpid = ""
        if p.vid is not None:
            vidpid = f"  VID:PID={p.vid:04X}:{p.pid:04X}"
        tag = ""
        if p.vid == CLOGGER_VID and p.pid == CLOGGER_PID:
            tag = "  <-- c-logger"
            match = p.device
        print(f"  [{i}] {p.device:8} {p.description}{vidpid}{tag}")

    if match:
        print(f"\nc-logger を自動検出: {match}")
        return match

    print("\n(c-logger=04D8:0B15 が見つかりませんでした。手動で選んでください)")
    sel = input("ポート番号 または COM名 を入力: ").strip()
    if sel.isdigit() and int(sel) < len(ports):
        return ports[int(sel)].device
    return sel


class Clogger:
    """c-logger との JSON コマンド通信。"""

    def __init__(self, port, timeout=2.0):
        print(f"ポートを開いています: {port} ...", flush=True)
        # USB-CDC なのでボーレートは無視されるが pyserial には必要。
        # write_timeout を入れて書き込みでハングしないようにする。
        self.ser = serial.Serial(port, baudrate=115200,
                                 timeout=timeout, write_timeout=timeout)
        print("ポートを開きました。", flush=True)
        time.sleep(0.2)
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass
        self._id = 0

    def cmd(self, command, params=None):
        """コマンドを送って応答(生文字列)を返す。応答なしは "" を返す。"""
        self._id += 1
        req = {"id": self._id, "command": command}
        if params is not None:
            req["params"] = params
        line = json.dumps(req) + "\n"
        try:
            self.ser.write(line.encode("ascii"))
        except serial.SerialTimeoutException:
            return "(書き込みタイムアウト)"
        # 先頭が '{' の行が来るまで最大数回読む(空行はタイムアウト)
        for _ in range(5):
            resp = self.ser.readline().decode("ascii", errors="replace").strip()
            if resp.startswith("{"):
                return resp
            if resp == "":
                break
        return resp

    def close(self):
        self.ser.close()


def do_dump(dev, save_csv=None):
    """dump: ヘッダ(JSON行) → 本体(バイナリ count*rec_size) → フッタ(JSON行) を受信して復元。"""
    dev._id += 1
    dev.ser.write((json.dumps({"id": dev._id, "command": "dump"}) + "\n").encode("ascii"))

    # ヘッダ(JSON行)を読む
    hdr = None
    for _ in range(5):
        line = dev.ser.readline().decode("ascii", errors="replace").strip()
        if line.startswith("{"):
            try:
                hdr = json.loads(line)
            except json.JSONDecodeError:
                hdr = None
            break
        if line == "":
            break
    if not hdr or hdr.get("dump") != "begin":
        print("dumpヘッダ取得失敗:", hdr)
        return

    count    = hdr.get("count", 0)
    rec_size = hdr.get("rec_size", 6)
    start_dt = hdr.get("start_dt", 0)
    interval = hdr.get("interval", 0)
    nbytes   = count * rec_size
    print(f"dump: count={count}, rec_size={rec_size}, start_dt={start_dt}, "
          f"interval={interval}, bytes={nbytes}")

    # 本体を「固定長」で読む(行ではなくバイト数で)
    raw = bytearray()
    while len(raw) < nbytes:
        chunk = dev.ser.read(nbytes - len(raw))
        if not chunk:
            print(f"  ⚠ タイムアウト: {len(raw)}/{nbytes} バイトのみ受信")
            break
        raw += chunk

    # フッタ(JSON行)を読む
    footer = dev.ser.readline().decode("ascii", errors="replace").strip()
    if '"dump":"end"' not in footer.replace(" ", ""):
        print("  ⚠ フッタ未検出:", footer)

    # 復元 (little-endian: int16 temp, uint16 hum, uint16 co2)
    n = len(raw) // rec_size
    rows = []
    for i in range(n):
        temp, hum, co2 = struct.unpack_from("<hHH", raw, i * rec_size)
        ts = start_dt + i * interval
        rows.append((i, ts, temp / 100.0, hum / 100.0, co2))

    def fmt(r):
        i, ts, t, h, c = r
        tstr = datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")  # ローカル時刻
        return f"  [{i}] {tstr}  temp={t:.2f}C hum={h:.2f}% co2={c}ppm"

    for r in rows[:5]:
        print(fmt(r))
    if len(rows) > 10:
        print(f"  ... ({len(rows) - 10} 件省略) ...")
        for r in rows[-5:]:
            print(fmt(r))
    print(f"{n} レコード受信")

    if save_csv:
        with open(save_csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["index", "epoch", "datetime", "temp_C", "hum_pct", "co2_ppm"])
            for i, ts, t, h, c in rows:
                tstr = datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")
                w.writerow([i, ts, tstr, f"{t:.2f}", f"{h:.2f}", c])
        print(f"CSV保存: {save_csv}")


def show(command, params, resp):
    p = json.dumps(params) if params else ""
    print(f"→ {command:13} {p}")
    if not resp:
        print("← (応答なし / タイムアウト)  ⚠")
        return
    print(f"← {resp}")
    try:
        j = json.loads(resp)
        if j.get("ok") is not True:
            print("  ⚠ ok=false / エラー応答")
    except json.JSONDecodeError:
        print("  ⚠ JSON として解釈できない応答")
    print()


def main():
    port = pick_port(sys.argv[1] if len(sys.argv) > 1 else None)
    print(f"接続中: {port}\n", flush=True)
    try:
        dev = Clogger(port)
    except serial.SerialException as e:
        print(f"ポートを開けません: {e}")
        print("→ 別のポート(もう一方のUSBシリアル)を試す / 他アプリがポートを開いていないか確認")
        sys.exit(1)

    # 安全なコマンドだけ自動テスト (start_logging は USB が切れるので含めない)
    tests = [
        ("ping", None),         # -> {"id":N,"ok":true}
        ("hello", None),        # -> {"id":N,"ok":true,"fw":"1.0.0","proto":1}
        ("get_battery", None),  # -> {"id":N,"ok":true,"vbat_mV":...}
        ("echo", {"n": 42}),    # -> {"id":N,"ok":true,"n":42}
        ("nosuchcmd", None),    # -> {"id":N,"ok":false,"error":"unknown_command"}
    ]
    print("=== 自動テスト ===", flush=True)
    for command, params in tests:
        show(command, params, dev.cmd(command, params))

    print("=== 対話モード ===  (空行 or 'quit' で終了)")
    print("使えるコマンド例:")
    print("  hello / ping / get_battery / get_status / clear_memory")
    print("  echo 7            n=7 を返す")
    print("  set_interval 60   計測間隔を60秒に")
    print("  set_time          現在のPC時刻(UNIX秒)を設定")
    print("  set_time 1750000000   指定epochを設定")
    print("  dump              保存データをバイナリ取得して表示")
    print("  dump out.csv      取得してCSV保存")
    print("  start_logging     計測開始(応答後USB切断。リセットで通信モードに戻る)")
    while True:
        try:
            s = input("cmd> ").strip()
        except (EOFError, KeyboardInterrupt):
            break
        if not s or s == "quit":
            break
        parts = s.split()
        command = parts[0]
        if command == "start_logging":
            print("  ⚠ ロギングモードに入り、応答(ack)の後にUSBが切断されます。")
            print("←", dev.cmd(command, None) or "(応答なし)")
            print("  デバイスはロギング中です。USBを抜いてOK。")
            print("  通信に戻すにはデバイスをリセット → このツールを再起動 → dump で読み出し。")
            continue
        if command == "dump":
            do_dump(dev, parts[1] if len(parts) > 1 else None)
            continue
        params = None
        try:
            if command == "echo" and len(parts) > 1:
                params = {"n": int(parts[1])}
            elif command == "set_interval" and len(parts) > 1:
                params = {"sec": int(parts[1])}
            elif command == "set_time":
                # 引数省略 or "now" → PCの現在時刻、数値ならその epoch
                if len(parts) > 1 and parts[1] != "now":
                    params = {"epoch": int(parts[1])}
                else:
                    params = {"epoch": int(time.time())}
        except ValueError:
            print("  引数が数値ではありません")
            continue
        print("←", dev.cmd(command, params) or "(応答なし)")

    dev.close()
    print("切断しました。")


if __name__ == "__main__":
    main()
