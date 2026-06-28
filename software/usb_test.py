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
    print("例:  hello   /   echo 7   /   get_battery")
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
            print("  ⚠ start_logging はロギングモードへ移行しUSBが切れます。送りません。")
            continue
        params = None
        if command == "echo" and len(parts) > 1:
            try:
                params = {"n": int(parts[1])}
            except ValueError:
                pass
        print("←", dev.cmd(command, params) or "(応答なし)")

    dev.close()
    print("切断しました。")


if __name__ == "__main__":
    main()
