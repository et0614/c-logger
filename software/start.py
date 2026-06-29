#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
c-logger 測定開始ツール (一般ユーザー向け)

USB接続した c-logger に対して以下を一括で実行し、計測を開始する:
  1. 既存の保存データを消去
  2. 時刻をPCの現在時刻に設定
  3. 計測間隔を設定 (既定 60秒)
  4. 計測開始 (この後USB通信は切断される)

使い方:
    python start.py                # 既定60秒間隔で開始
    python start.py -i 300         # 5分(300秒)間隔で開始
    python start.py -p COM3        # ポートを明示指定

計測を止めてデータを読み出すには、デバイスをリセットして
USB通信モードに戻してから read.py を実行する。

依存: pip install pyserial
"""
import sys
import json
import time
import argparse

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("pyserial が必要です:  pip install pyserial")
    sys.exit(1)

# c-logger の USB 識別子 (firmware の usb_descriptors)
CLOGGER_VID = 0x04D8
CLOGGER_PID = 0x0B15

DEFAULT_INTERVAL = 60  # 既定の計測間隔 [秒]


def find_port():
    """c-logger(VID:PID=04D8:0B15) の COM ポートを自動検出する。無ければ None。"""
    for p in list_ports.comports():
        if p.vid == CLOGGER_VID and p.pid == CLOGGER_PID:
            return p.device
    return None


class Clogger:
    """c-logger との JSON コマンド通信 (1行=1コマンド, '\\n' 終端)。"""

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

    def cmd(self, command, params=None):
        """コマンドを送り応答(dict)を返す。応答なし/ok=false は例外。"""
        self._id += 1
        req = {"id": self._id, "command": command}
        if params is not None:
            req["params"] = params
        self.ser.write((json.dumps(req) + "\n").encode("ascii"))

        resp = ""
        for _ in range(5):
            line = self.ser.readline().decode("ascii", errors="replace").strip()
            if line.startswith("{"):
                resp = line
                break
            if line == "":
                break
        if not resp:
            raise RuntimeError(f"'{command}' への応答がありません(タイムアウト)")
        j = json.loads(resp)
        if j.get("ok") is not True:
            raise RuntimeError(f"'{command}' が失敗しました: {j.get('error', resp)}")
        return j

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser(
        description="c-logger の計測を開始する(消去+時刻設定+間隔設定+開始 を一括実行)")
    ap.add_argument("-i", "--interval", type=int, default=DEFAULT_INTERVAL,
                    help=f"計測間隔[秒] (既定 {DEFAULT_INTERVAL})")
    ap.add_argument("-p", "--port", default=None,
                    help="COMポート (省略時は自動検出)")
    args = ap.parse_args()

    if args.interval <= 0:
        print("計測間隔は1秒以上を指定してください。")
        return 1

    port = args.port or find_port()
    if port is None:
        print("c-logger が見つかりません。USB接続を確認してください。")
        return 1

    try:
        dev = Clogger(port)
    except serial.SerialException as e:
        print(f"ポート {port} を開けません: {e}")
        print("他アプリがポートを使用していないか確認してください。")
        return 1

    try:
        print(f"接続: {port}")
        mv = dev.cmd("get_battery").get("vbat_mV")
        print(f"電池電圧: {mv} mV")

        print("既存の保存データを消去しています...")
        dev.cmd("clear_memory")

        dev.cmd("set_time", {"epoch": int(time.time())})
        print("時刻をPCの現在時刻に設定しました。")

        dev.cmd("set_interval", {"sec": args.interval})
        print(f"計測間隔: {args.interval} 秒")

        dev.cmd("start_logging")
        print("\n計測を開始しました。USB通信は切断されます。")
        print("（最初の約22秒はセンサのコンディショニングを行い、その後に記録を開始します）")
        print("→ データを読み出すには: デバイスをリセット後、read.py を実行してください。")
    except (RuntimeError, json.JSONDecodeError) as e:
        print(f"エラー: {e}")
        return 1
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
