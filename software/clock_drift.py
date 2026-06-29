#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
c-logger ソフトウェア時計のドリフト計測ツール (開発者向け)

USB接続したまま、デバイス内部の時計 (g_epoch) の進み方が実時間 (PC=NTP同期) に
対してどれだけズレるかを ppm で測る。リセットやCSVを介さないので、5秒長押し等の
曖昧さが入らない。

測定原理:
  デバイスの epoch は1秒ごとに +1 されるだけ (PIT=水晶駆動)。これを2回、
  「epochが N→N+1 に変化した瞬間」に PC時刻でピン留めして端点とし、
      ppm = (デバイス経過秒 − PC経過秒) / PC経過秒 × 1e6
  を計算する。両端を変化点に固定するので ±1秒の量子化が消える。
  端点の高速ポーリング以外は無通信で待つ (USB起きっぱなしの消費は変わらないが
  通信トラフィックは最小)。

使い方:
    python clock_drift.py                # 既定30分
    python clock_drift.py -d 60          # 60分
    python clock_drift.py -d 0.5         # 30秒 (動作確認用)
    python clock_drift.py -p COM3        # ポート明示
    python clock_drift.py --log drift.csv  # 結果を追記保存

注意:
  * 測定前に PC を NTP同期させておくこと (基準が PC の time.time())。
  * デバイスは USB通信モード (start_logging していない状態) で実行すること。
  * 測定中は抜線・リセットしないこと。Ctrl+C で打ち切っても、その時点までで計算する。
  * 必要分解能の目安: 端点ジッタ(往復~数十ms)×2 / 測定時間。
      30分なら ~50ppm、60分なら ~25ppm 程度。-370ppm の判別には30分で十分。

切り分け:
  * ここで ≈ -370ppm が出る → 水晶/クロック源が原因 (ロギングでも同じ。要 補正)。
  * ここは ≈ 0〜-20ppm なのにロギングは -370ppm → ロギング固有のティック取りこぼし。

依存: pip install pyserial
"""
import sys
import json
import time
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

    def get_epoch(self):
        """デバイスのソフトウェア時計 (UNIX epoch秒, 整数) を取得する。"""
        return int(self.cmd("get_status")["epoch"])

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


def capture_transition(dev, timeout=3.0):
    """epoch が変化する瞬間を PC時刻でピン留めする。

    epoch が変わる直前(古値)と直後(新値)の読み取り時刻の中点を遷移時刻とみなす。
    @return (t_mid, epoch_new, bracket)
       t_mid    : 遷移時刻の推定 (PC time.time()), 真値の最良推定
       epoch_new: 遷移後の epoch
       bracket  : 遷移をはさむ読み取り間隔[s] (端点誤差の指標。誤差 <= bracket/2)
    """
    e0 = dev.get_epoch()
    t_old = time.time()          # 古値を見た最後の時刻
    deadline = time.time() + timeout
    while time.time() < deadline:
        e = dev.get_epoch()
        t = time.time()
        if e != e0:
            return 0.5 * (t_old + t), e, (t - t_old)
        t_old = t
    raise RuntimeError("epoch が変化しません (時計が進んでいない/ロギング中?)")


def fmt_dur(sec):
    sec = int(round(sec))
    h, rem = divmod(sec, 3600)
    m, s = divmod(rem, 60)
    if h:
        return f"{h}h{m:02d}m{s:02d}s"
    if m:
        return f"{m}m{s:02d}s"
    return f"{s}s"


def main():
    ap = argparse.ArgumentParser(
        description="c-logger 内部時計のドリフトを USB据え置きで ppm 計測する")
    ap.add_argument("-d", "--minutes", type=float, default=30.0,
                    help="測定時間[分] (既定 30)")
    ap.add_argument("-p", "--port", default=None,
                    help="COMポート (省略時は自動検出)")
    ap.add_argument("--log", default=None,
                    help="結果を追記保存するCSVファイル名 (任意)")
    args = ap.parse_args()

    if args.minutes <= 0:
        print("測定時間は正の値を指定してください。")
        return 1

    port = args.port or find_port()
    if port is None:
        print("c-logger が見つかりません。USB接続を確認してください。")
        print("(ロギング中の場合はUSB通信モードに戻してから実行してください)")
        return 1

    try:
        dev = Clogger(port)
    except serial.SerialException as e:
        print(f"ポート {port} を開けません: {e}")
        print("他アプリがポートを使用していないか確認してください。")
        return 1

    target = args.minutes * 60.0
    try:
        print(f"接続: {port}")
        print("※ PCがNTP同期していることを確認してください (基準=PC時刻)。")
        print(f"測定時間: 約{fmt_dur(target)}  (Ctrl+Cで途中打ち切り可)\n")

        # --- 始点をピン留め ---
        print("始点を取得中 (epoch変化待ち)...")
        t0, e0, b0 = capture_transition(dev)
        print(f"  始点: device_epoch={e0}  PC={datetime.fromtimestamp(t0):%H:%M:%S.%f}"
              f"  (±{b0/2*1000:.0f}ms)\n")

        # --- 中間は無通信で待機 (端点のみ高速ポーリング) ---
        wait_until = t0 + target
        try:
            while True:
                remaining = wait_until - time.time()
                if remaining <= 0:
                    break
                print(f"\r待機中... 残り {fmt_dur(remaining)}   ", end="", flush=True)
                time.sleep(min(2.0, remaining))
            print()
        except KeyboardInterrupt:
            print("\n中断: ここまでで計算します。")

        # --- 終点をピン留め ---
        print("終点を取得中 (epoch変化待ち)...")
        t1, e1, b1 = capture_transition(dev)
        print(f"  終点: device_epoch={e1}  PC={datetime.fromtimestamp(t1):%H:%M:%S.%f}"
              f"  (±{b1/2*1000:.0f}ms)\n")

        # --- 計算 ---
        dev_elapsed = e1 - e0          # 整数秒 (両端を変化点に固定したので正確)
        real_elapsed = t1 - t0         # PC秒 (NTP基準=真値)
        if real_elapsed <= 0:
            print("経過時間が0以下です。測定時間を長くしてください。")
            return 1

        diff = dev_elapsed - real_elapsed   # 負: デバイスが遅れている
        ppm = diff / real_elapsed * 1e6
        unc_sec = 0.5 * (b0 + b1)           # 端点誤差(最悪)の合計
        ppm_unc = unc_sec / real_elapsed * 1e6
        sec_per_day = ppm / 1e6 * 86400.0

        print("==== 結果 ====")
        print(f"デバイス経過 : {dev_elapsed} s")
        print(f"PC経過(真値) : {real_elapsed:.3f} s")
        print(f"差           : {diff:+.3f} s  (負=デバイスが遅い)")
        print(f"ドリフト     : {ppm:+.1f} ppm  (±{ppm_unc:.1f} ppm 測定分解能)")
        print(f"             ≒ {sec_per_day:+.2f} s/日")
        print(f"             ≒ {ppm/1e6*54000:+.1f} s / 54000s")

        # --- 簡易判定 ---
        print()
        if abs(ppm) <= max(2 * ppm_unc, 30):
            print("判定: ほぼ正常 (数十ppm以内)。-370ppm級のズレはこのモードでは出ていない。")
            print("      → 元の -20s はロギング固有(ティック取りこぼし)の可能性が高い。")
        else:
            print(f"判定: 有意なドリフト ({ppm:+.0f} ppm)。クロック源(水晶/PIT)自体が"
                  f"{'遅い' if ppm < 0 else '速い'}。")
            print("      → ロギングでも同じズレ。RTC水晶誤差補正(CORREN)等での補正が必要。")

        # --- ログ追記 ---
        if args.log:
            new = False
            try:
                with open(args.log, "r"):
                    pass
            except FileNotFoundError:
                new = True
            with open(args.log, "a", newline="") as f:
                if new:
                    f.write("start_pc,end_pc,dev_elapsed_s,real_elapsed_s,"
                            "diff_s,ppm,ppm_unc,sec_per_day\n")
                f.write(f"{datetime.fromtimestamp(t0):%Y-%m-%d %H:%M:%S},"
                        f"{datetime.fromtimestamp(t1):%Y-%m-%d %H:%M:%S},"
                        f"{dev_elapsed},{real_elapsed:.3f},{diff:.3f},"
                        f"{ppm:.1f},{ppm_unc:.1f},{sec_per_day:.3f}\n")
            print(f"\n結果を追記: {args.log}")

    except (RuntimeError, json.JSONDecodeError, KeyError) as e:
        print(f"\nエラー: {e}")
        return 1
    finally:
        dev.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
