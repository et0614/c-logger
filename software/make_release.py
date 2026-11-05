#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
配布用 zip (CLogger_vX.Y.Z.zip) を作る。GitHub Actions (.github/workflows/release.yml) から呼ばれる。

zip の中身:
    CLogger.exe                 PCツール (GUI)。ファームウェア更新用に avrdude を同梱
    clogger_firmware_X.Y.Z.hex  配布するファームウェア (firmware/release/clogger.hex)
    COPYING_avrdude.txt         avrdude のライセンス (GPL v2)
    README.txt                  使い方の案内

使い方:
    python make_release.py --avrdude <avrdude.exe と avrdude.conf のあるフォルダ>
    python make_release.py --avrdude . -o out.zip

依存: pip install pyserial pyinstaller
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent                    # software/
REPO = ROOT.parent
FW_HEX = REPO / "firmware" / "release" / "clogger.hex"
VERSION_H = REPO / "firmware" / "clogger.X" / "version.h"
MANUAL_URL = "https://github.com/et0614/c-logger/blob/main/docs/manual.md"

README = """C-Logger tools (firmware {fw})

CLogger.exe
  PC tool for the C-Logger (start logging, save data as CSV, clock correction, firmware update).
  Double-click to start. If Windows SmartScreen appears, choose "More info" -> "Run anyway".

clogger_firmware_{fw}.hex
  Firmware. To update: Firmware tab of CLogger.exe.

Manual (Japanese): {url}

CLogger.exe contains avrdude {avrdude} (GPL v2) for the firmware update. See COPYING_avrdude.txt.
The source code of the bundled avrdude is available on request for three years from this release
(GitHub Issues: https://github.com/et0614/c-logger/issues).

----
C-Logger ツール (ファームウェア {fw})

CLogger.exe: PCツール。記録開始、CSV保存、時計の補正、ファームウェア更新ができます。
clogger_firmware_{fw}.hex: ファームウェア。CLogger.exe の Firmware タブで書き込みます。
取扱説明書: {url}
"""


def fw_version():
    m = re.search(r'FW_VERSION\s+"([^"]+)"', VERSION_H.read_text(encoding="utf-8"))
    return m.group(1)


def check_hex_version(fw):
    """配布する hex が version.h と同じ版のビルドか、hex 内の文字列で確かめる"""
    data = bytearray()
    for line in FW_HEX.read_text(encoding="ascii").splitlines():
        if line.startswith(":") and line[7:9] == "00":
            data += bytes.fromhex(line[9:-2])
    return fw.encode() in data


def avrdude_version(exe):
    out = subprocess.run([str(exe), "-?"], capture_output=True, text=True, errors="replace")
    m = re.search(r"version\s+([\d.]+)", out.stdout + out.stderr, re.I)
    return m.group(1) if m else "?"


def main():
    ap = argparse.ArgumentParser(description="Build the C-Logger release zip")
    ap.add_argument("--avrdude", required=True, help="folder containing avrdude.exe and avrdude.conf")
    ap.add_argument("-o", "--output", help="output zip (default: software/CLogger_v<fw>.zip)")
    args = ap.parse_args()

    fw = fw_version()
    avr = Path(args.avrdude).resolve()  # PyInstaller は spec のある一時フォルダを基準にするので絶対パスにする
    need = [avr / "avrdude.exe", avr / "avrdude.conf", FW_HEX, ROOT / "COPYING_avrdude.txt"]
    missing = [str(p) for p in need if not p.is_file()]
    if missing:
        print("missing:\n  " + "\n  ".join(missing))
        return 1
    if not check_hex_version(fw):
        print(f"firmware/release/clogger.hex is not FW {fw} (version.h). "
              "Copy the production hex of this version to firmware/release/clogger.hex.")
        return 1
    out = Path(args.output) if args.output else ROOT / f"CLogger_v{fw}.zip"

    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        sep = ";" if sys.platform.startswith("win") else ":"
        subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--onefile", "--windowed",
                        "--name", "CLogger",
                        "--add-binary", f"{avr / 'avrdude.exe'}{sep}.",
                        "--add-data", f"{avr / 'avrdude.conf'}{sep}.",
                        "--distpath", str(td / "dist"), "--workpath", str(td / "build"),
                        "--specpath", str(td), str(ROOT / "clogger_gui.py")], check=True)
        stage = td / "CLogger"
        stage.mkdir()
        shutil.copy2(td / "dist" / "CLogger.exe", stage / "CLogger.exe")
        shutil.copy2(FW_HEX, stage / f"clogger_firmware_{fw}.hex")
        shutil.copy2(ROOT / "COPYING_avrdude.txt", stage / "COPYING_avrdude.txt")
        (stage / "README.txt").write_text(
            README.format(fw=fw, url=MANUAL_URL, avrdude=avrdude_version(avr / "avrdude.exe")),
            encoding="utf-8-sig", newline="\r\n")
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
            for f in sorted(stage.iterdir()):
                z.write(f, f"CLogger/{f.name}")
    print(f"{out} (firmware {fw})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
