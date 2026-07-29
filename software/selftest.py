#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
c-logger セルフテスト / ライブ計測ツール (開発者向け)

USB接続したまま、温湿度センサ・CO2センサ(STCC4)・フラッシュメモリが正常かを確認する。
計測器(電流計等)につないだまま実行できる。ロギングモードには入らない(USBは切れない)。

使い方:
    python selftest.py            # セルフテストを1回実行し pass/fail 表を表示
    python selftest.py --watch    # co2/温湿度を繰り返しライブ表示 (Ctrl+Cで停止)
    python selftest.py --watch 5  # 5秒間隔でライブ表示
    python selftest.py -p COM3    # ポート明示

依存: pip install pyserial

対応 firmware コマンド:
    selftest -> {"flash":{ok,rw,jedec}, "stcc4":{connected}, "sensor":{ok,co2_ppm,temp_C,hum_pct}}
    measure  -> {"co2_ppm","temp_C","hum_pct"}
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

CLOGGER_VID = 0x04D8
CLOGGER_PID = 0x0B15


def find_port():
    for p in list_ports.comports():
        if p.vid == CLOGGER_VID and p.pid == CLOGGER_PID:
            return p.device
    return None


class Clogger:
    """c-logger との JSON コマンド通信。request() は ok=false でも dict を返す(自己診断表示のため)。"""

    def __init__(self, port, timeout=20.0):  # 無応答個体はI2C/SPIのタイムアウトが積み重なり数秒〜十数秒かかるため長めに
        self.ser = serial.Serial(port, baudrate=115200,
                                 timeout=timeout, write_timeout=timeout)
        time.sleep(0.2)
        try:
            self.ser.reset_input_buffer()
        except Exception:
            pass
        self._id = 0

    def request(self, command, params=None):
        """コマンドを送り応答(dict)を返す。応答なしは RuntimeError。ok=false でも dict を返す。"""
        self._id += 1
        req = {"id": self._id, "command": command}
        if params is not None:
            req["params"] = params
        self.ser.write((json.dumps(req) + "\n").encode("ascii"))
        for _ in range(6):
            line = self.ser.readline().decode("ascii", errors="replace").strip()
            if line.startswith("{"):
                return json.loads(line)
            if line == "":
                break
        raise RuntimeError(f"'{command}' への応答がありません(タイムアウト)")

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def _mark(ok):
    return "OK  " if ok else "NG  "


def run_selftest(dev):
    j = dev.request("selftest")
    flash = j.get("flash", {})
    stcc4 = j.get("stcc4", {})
    sensor = j.get("sensor", {})

    print("==== セルフテスト結果 ====")
    print(f"  [{_mark(flash.get('ok') and flash.get('rw'))}] フラッシュ  "
          f"JEDEC={flash.get('jedec')}  (id={flash.get('ok')} / write-read-verify={flash.get('rw')})")
    print(f"  [{_mark(stcc4.get('connected'))}] STCC4(CO2)  I2C接続={stcc4.get('connected')}")
    s_ok = sensor.get("ok")
    if s_ok:
        print(f"  [{_mark(True)}] 計測        "
              f"CO2={sensor.get('co2_ppm')}ppm  T={sensor.get('temp_C')}C  RH={sensor.get('hum_pct')}%")
    else:
        print(f"  [{_mark(False)}] 計測        失敗 (STCC4/SHT4x 応答なし or CRCエラー)")

    ok = bool(j.get("ok"))
    print(f"\n総合判定: {'PASS ✅' if ok else 'FAIL ❌'}")
    # 参考コメント
    if s_ok and sensor.get("co2_ppm") in (390, 391, 389):
        print("  注: CO2が~390ppmは電源投入直後のbypass位相の可能性(最初の2回)。数回 measure してください。")
    if not (flash.get('ok') and flash.get('rw')):
        print("  → フラッシュNG: SPI配線/CS/はんだ、電源を確認。")
    if not stcc4.get('connected'):
        print("  → STCC4未応答: I2C配線(SDA/SCL)/電源/アドレス(0x64)を確認。")
    return ok


def run_watch(dev, interval):
    print(f"ライブ計測 (interval={interval}s, Ctrl+Cで停止)")
    print("  time      CO2[ppm]  T[C]    RH[%]")
    n = 0
    try:
        while True:
            j = dev.request("measure")
            n += 1
            if j.get("ok"):
                print(f"  {time.strftime('%H:%M:%S')}  {str(j.get('co2_ppm')):>7}  "
                      f"{str(j.get('temp_C')):>6}  {str(j.get('hum_pct')):>6}")
            else:
                print(f"  {time.strftime('%H:%M:%S')}  計測失敗: {j.get('error')}")
            # measure自体が~500msブロックするので、残り時間だけ待つ
            time.sleep(max(0.0, interval - 0.5))
    except KeyboardInterrupt:
        print(f"\n停止 ({n}回計測)。")


def main():
    ap = argparse.ArgumentParser(
        description="c-logger のセンサ/フラッシュを USB接続のまま自己診断する")
    ap.add_argument("--watch", nargs="?", const=2.0, type=float, default=None,
                    metavar="SEC",
                    help="co2/温湿度をライブ表示 (既定2秒間隔, Ctrl+Cで停止)")
    ap.add_argument("-p", "--port", default=None, help="COMポート (省略時は自動検出)")
    args = ap.parse_args()

    port = args.port or find_port()
    if port is None:
        print("c-logger が見つかりません。USB接続を確認してください。")
        print("(ロギング中の場合はリセットして USB通信モードに戻してください)")
        return 1

    try:
        dev = Clogger(port)
    except serial.SerialException as e:
        print(f"ポート {port} を開けません: {e}")
        return 1

    try:
        print(f"接続: {port}")
        if args.watch is not None:
            run_watch(dev, args.watch)
            return 0
        ok = run_selftest(dev)
        return 0 if ok else 2
    except (RuntimeError, json.JSONDecodeError, KeyError) as e:
        print(f"エラー: {e}")
        return 1
    finally:
        dev.close()


if __name__ == "__main__":
    sys.exit(main())
