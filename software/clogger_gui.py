#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
C-Logger GUI ツール (tkinter, 画面表示は英語)

clogger.py の CLogger クラス(USB-CDC / JSONプロトコル)を使い、以下を1画面で行う。
  - 機器情報の表示 (FW, 機体ID, 電池電圧, 保存件数, センサ構成)
  - 記録開始 (データ消去 + 時刻合わせ + 記録間隔)
  - データ読み出し (CSV)、データ消去
  - 温湿度 (CO2) のリアルタイム表示
  - 時計の補正、温度・湿度・CO2 の補正係数、CO2 の強制校正 (FRC)
  - 自己診断、ファームウェア更新

通信はすべてワーカースレッドで行い、結果はキュー経由でUIスレッドに渡す (tkinterはUIスレッド専用)。
機器へのアクセスは Device.lock で直列化する (リアルタイム表示中に他の操作をしても衝突しない)。

実行: python clogger_gui.py   (依存: pyserial)
exe化: build_exe.bat (PyInstaller)
"""
import queue
import threading
import time
from datetime import datetime

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

import serial
from serial.tools import list_ports

import clogger as cl

APP_TITLE = "C-Logger"
LIVE_INTERVAL_S = 1.0          # リアルタイム表示の測定間隔
INTERVAL_MAX_S = 3600          # 記録間隔の上限
AUTO_PORT = "Auto"


def fmt_time(epoch):
    return datetime.fromtimestamp(epoch).strftime("%Y-%m-%d %H:%M:%S")


class Device:
    """CLogger をスレッド間で共有するための入れ物 (操作は lock で直列化)"""

    def __init__(self):
        self.dev = None
        self.port = None
        self.lock = threading.Lock()

    @property
    def connected(self):
        return self.dev is not None

    def open(self, port):
        dev = cl.CLogger(port)
        try:
            h = dev.request("hello")
        except Exception:
            dev.close()
            raise
        if h.get("device") != cl.DEVICE_NAME:
            dev.close()
            raise RuntimeError(f"{port} is not a C-Logger (or its firmware is older than 3.0.0)")
        with self.lock:
            self.dev, self.port = dev, port

    def close(self):
        with self.lock:
            if self.dev:
                self.dev.close()
            self.dev, self.port = None, None

    def call(self, fn):
        """fn(CLogger) を排他実行する"""
        with self.lock:
            if self.dev is None:
                raise RuntimeError("not connected")
            return fn(self.dev)


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_TITLE)
        self.minsize(600, 480)
        self.device = Device()
        self.q = queue.Queue()
        self.busy = False
        self.info = {}
        self.live_on = False
        self.live_thread = None

        self._build_ui()
        self._set_connected(False)
        self.after(50, self._poll_queue)
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(200, self._refresh_ports)

    # ------------------------------------------------------------------
    # UI 構築
    # ------------------------------------------------------------------
    def _build_ui(self):
        pad = {"padx": 6, "pady": 3}

        # 接続
        top = ttk.Frame(self)
        top.pack(fill="x", padx=8, pady=(8, 4))
        ttk.Label(top, text="Port:").pack(side="left")
        self.port_var = tk.StringVar(value=AUTO_PORT)
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=12, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(top, text="↻", width=3, command=self._refresh_ports).pack(side="left")
        self.connect_btn = ttk.Button(top, text="Connect", command=self._on_connect)
        self.connect_btn.pack(side="left", padx=6)
        self.refresh_btn = ttk.Button(top, text="Refresh", command=self._refresh_info)
        self.refresh_btn.pack(side="left")
        self.device_buttons = [self.refresh_btn]  # 接続中だけ有効にするボタン (各タブで追加)

        # 機器情報
        box = ttk.LabelFrame(self, text="Device")
        box.pack(fill="x", padx=8, pady=4)
        self.info_vars = {}
        fields = [("device", "Device"), ("id", "Device ID"), ("battery", "Battery"),
                  ("records", "Stored records"), ("period", "Recorded period"), ("sensor", "Sensors")]
        for i, (key, label) in enumerate(fields):
            r, c = divmod(i, 2)
            ttk.Label(box, text=label + ":").grid(row=r, column=c * 2, sticky="w", **pad)
            v = tk.StringVar(value="-")
            ttk.Label(box, textvariable=v).grid(row=r, column=c * 2 + 1, sticky="w", **pad)
            self.info_vars[key] = v
        box.columnconfigure(1, weight=1)
        box.columnconfigure(3, weight=1)

        # タブ
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=8, pady=4)
        self._build_logging_tab()
        self._build_data_tab()
        self._build_live_tab()
        self._build_clock_tab()
        self._build_calib_tab()
        self._build_diag_tab()
        self._build_firmware_tab()

        # ステータスバー
        bar = ttk.Frame(self)
        bar.pack(fill="x", padx=8, pady=(0, 8))
        self.status_var = tk.StringVar(value="Not connected.")
        ttk.Label(bar, textvariable=self.status_var).pack(side="left")
        self.progress = ttk.Progressbar(bar, length=180, mode="determinate")
        self.progress.pack(side="right")

    def _build_logging_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Logging")
        ttk.Label(f, text="Recording interval (s):").grid(row=0, column=0, sticky="w")
        self.interval_var = tk.IntVar(value=60)
        sp = ttk.Spinbox(f, from_=1, to=INTERVAL_MAX_S, width=6, textvariable=self.interval_var,
                         command=self._update_capacity)
        sp.grid(row=0, column=1, sticky="w", padx=6)
        sp.bind("<KeyRelease>", lambda e: self._update_capacity())
        self.capacity_var = tk.StringVar()
        ttk.Label(f, textvariable=self.capacity_var, foreground="gray").grid(row=1, column=0, columnspan=3,
                                                                           sticky="w", pady=(2, 10))
        self.start_btn = ttk.Button(f, text="Start logging…", command=self._on_start_logging)
        self.start_btn.grid(row=2, column=0, sticky="w")
        ttk.Label(f, wraplength=540, justify="left", text=(
            "Starting erases all stored data and synchronizes the device clock to this PC.\n"
            "After starting, the C-Logger disconnects from USB and keeps logging on its coin cell (CR2032). "
            "To stop logging, unplug and plug it into USB again (data is kept) "
            "or hold the button for 5 seconds.")).grid(row=3, column=0, columnspan=3, sticky="w", pady=10)
        self.device_buttons.append(self.start_btn)
        self._update_capacity()

    def _build_data_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Data")
        self.save_btn = ttk.Button(f, text="Save as CSV…", command=self._on_save_csv)
        self.save_btn.grid(row=0, column=0, sticky="w")
        ttk.Label(f, wraplength=540, justify="left", foreground="gray", text=(
            "CSV columns: index, epoch, datetime, temp_C, hum_pct, co2_ppm, temp_raw_C, hum_raw_pct, co2_raw_ppm\n"
            "temp_C to co2_ppm are corrected with the coefficients stored in the device (Calibration tab); "
            "*_raw are the values as recorded.\n"
            "Failed measurements have empty values. CO2 columns are empty on the model without a CO2 sensor."
        )).grid(row=1, column=0, sticky="w", pady=10)
        ttk.Separator(f).grid(row=2, column=0, sticky="ew", pady=10)
        self.erase_btn = ttk.Button(f, text="Erase all data…", command=self._on_erase)
        self.erase_btn.grid(row=3, column=0, sticky="w")
        f.columnconfigure(0, weight=1)
        self.device_buttons += [self.save_btn, self.erase_btn]

    def _build_live_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Live view")
        self.live_btn = ttk.Button(f, text="Start", command=self._toggle_live)
        self.live_btn.grid(row=0, column=0, sticky="w")
        big = ("Segoe UI", 30, "bold")
        self.live_temp = tk.StringVar(value="--.-- °C")
        self.live_hum = tk.StringVar(value="--.-- %RH")
        self.live_co2 = tk.StringVar(value="")
        ttk.Label(f, textvariable=self.live_temp, font=big).grid(row=1, column=0, sticky="w", pady=(10, 0))
        ttk.Label(f, textvariable=self.live_hum, font=big).grid(row=2, column=0, sticky="w")
        ttk.Label(f, textvariable=self.live_co2, font=big).grid(row=3, column=0, sticky="w")
        ttk.Label(f, wraplength=540, justify="left", foreground="gray", text=(
            "Readings are not saved. Values are corrected with the coefficients in the Calibration tab. "
            "Right after connecting, the sensor may read high "
            "because the board is warmed by USB power.")).grid(row=4, column=0, sticky="w", pady=10)
        self.device_buttons.append(self.live_btn)

    def _build_clock_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Clock")
        ttk.Label(f, wraplength=540, justify="left", text=(
            "The device clock runs on a 32.768 kHz crystal, which may drift by up to about ±20 ppm "
            "(±1.7 s/day). Measure the drift against this PC's clock and store a correction in the device.\n"
            "Synchronize the PC clock with an internet time server first, and keep the room temperature steady.")
        ).grid(row=0, column=0, columnspan=3, sticky="w")
        self.clock_cur = tk.StringVar(value="Current correction: -")
        ttk.Label(f, textvariable=self.clock_cur).grid(row=1, column=0, columnspan=3, sticky="w", pady=(8, 4))
        row = ttk.Frame(f)
        row.grid(row=2, column=0, columnspan=3, sticky="w", pady=4)
        ttk.Label(row, text="Measuring time (min):").pack(side="left")
        self.clock_min = tk.StringVar(value="60")
        ttk.Combobox(row, textvariable=self.clock_min, values=["10", "30", "60", "120"], width=5,
                     state="readonly").pack(side="left", padx=6)
        self.clock_btn = ttk.Button(row, text="Measure", command=self._on_clock_measure)
        self.clock_btn.pack(side="left", padx=6)
        self.clock_stop_btn = ttk.Button(row, text="Stop", command=self._on_clock_stop, state="disabled")
        self.clock_stop_btn.pack(side="left")
        self.clock_result = tk.StringVar(value="")
        ttk.Label(f, textvariable=self.clock_result, font=("Consolas", 10), justify="left").grid(
            row=3, column=0, columnspan=3, sticky="w", pady=6)
        btns = ttk.Frame(f)
        btns.grid(row=4, column=0, columnspan=3, sticky="w")
        self.clock_apply_btn = ttk.Button(btns, text="Apply correction", command=self._on_clock_apply)
        self.clock_apply_btn.pack(side="left")
        self.clock_zero_btn = ttk.Button(btns, text="Reset correction to 0",
                                         command=lambda: self._set_clock_calib(0))
        self.clock_zero_btn.pack(side="left", padx=6)
        ttk.Label(f, wraplength=540, justify="left", foreground="gray", text=(
            "A longer measurement is more accurate (60 min gives about ±1 ppm). "
            "To check the result, measure again after applying: the drift should be close to 0.")
        ).grid(row=5, column=0, columnspan=3, sticky="w", pady=8)
        self.clock_new = None
        self.clock_stop_flag = False
        self.device_buttons += [self.clock_btn, self.clock_zero_btn]

    def _build_calib_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Calibration")
        ttk.Label(f, wraplength=560, justify="left", text=(
            "Linear correction of the readings: corrected = a × raw + b. "
            "The coefficients are stored in the device. The device records raw values, and this tool applies the "
            "correction when it shows or saves data, so changed coefficients also apply to data already recorded.")
        ).grid(row=0, column=0, columnspan=4, sticky="w")
        ttk.Label(f, text="a").grid(row=1, column=1, pady=(8, 0))
        ttk.Label(f, text="b").grid(row=1, column=2, pady=(8, 0))
        self.corr_vars = {}
        rows = [("temp", "Temperature (°C)"), ("hum", "Humidity (%RH)"), ("co2", "CO2 (ppm)")]
        for i, (k, label) in enumerate(rows):
            ttk.Label(f, text=label).grid(row=2 + i, column=0, sticky="w", padx=(0, 8))
            va, vb = tk.StringVar(value="1.00000"), tk.StringVar(value="0.00")
            ttk.Entry(f, textvariable=va, width=10).grid(row=2 + i, column=1, padx=4, pady=2)
            ttk.Entry(f, textvariable=vb, width=10).grid(row=2 + i, column=2, padx=4, pady=2)
            self.corr_vars[k] = (va, vb)
        btns = ttk.Frame(f)
        btns.grid(row=5, column=0, columnspan=4, sticky="w", pady=6)
        self.corr_save_btn = ttk.Button(btns, text="Save to device", command=self._on_corr_save)
        self.corr_save_btn.pack(side="left")
        self.corr_reset_btn = ttk.Button(btns, text="Reset (no correction)", command=self._on_corr_reset)
        self.corr_reset_btn.pack(side="left", padx=6)
        ttk.Label(f, foreground="gray", text="a: 0.8 to 1.2.  b: up to ±5 °C, ±15 %RH, ±2000 ppm.").grid(
            row=6, column=0, columnspan=4, sticky="w")

        ttk.Separator(f).grid(row=7, column=0, columnspan=4, sticky="ew", pady=10)
        ttk.Label(f, text="CO2 forced recalibration (FRC)", font=("Segoe UI", 9, "bold")).grid(
            row=8, column=0, columnspan=4, sticky="w")
        ttk.Label(f, wraplength=560, justify="left", text=(
            "Adjusts the CO2 sensor itself to a known CO2 concentration (stored in the sensor). "
            "Place the C-Logger where the CO2 concentration is known and stable, for example outdoors "
            "(about 420 ppm), and keep people away. The tool measures for about a minute, then recalibrates.")
        ).grid(row=9, column=0, columnspan=4, sticky="w")
        row = ttk.Frame(f)
        row.grid(row=10, column=0, columnspan=4, sticky="w", pady=6)
        ttk.Label(row, text="Reference CO2 (ppm):").pack(side="left")
        self.frc_ppm = tk.StringVar(value=str(cl.FRC_DEFAULT_PPM))
        ttk.Entry(row, textvariable=self.frc_ppm, width=7).pack(side="left", padx=6)
        self.frc_btn = ttk.Button(row, text="Recalibrate…", command=self._on_frc)
        self.frc_btn.pack(side="left", padx=6)
        self.frc_stop_btn = ttk.Button(row, text="Stop", command=self._on_frc_stop, state="disabled")
        self.frc_stop_btn.pack(side="left")
        self.frc_result = tk.StringVar(value="")
        ttk.Label(f, textvariable=self.frc_result).grid(row=11, column=0, columnspan=4, sticky="w")
        self.frc_stop_flag = False
        self.device_buttons += [self.corr_save_btn, self.corr_reset_btn, self.frc_btn]

    def _build_diag_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Diagnostics")
        self.selftest_btn = ttk.Button(f, text="Run self-test", command=self._on_selftest)
        self.selftest_btn.grid(row=0, column=0, sticky="w")
        self.selftest_var = tk.StringVar(value="")
        ttk.Label(f, textvariable=self.selftest_var, font=("Consolas", 10), justify="left").grid(
            row=1, column=0, sticky="w", pady=6)
        self.device_buttons.append(self.selftest_btn)

    def _build_firmware_tab(self):
        f = ttk.Frame(self.nb, padding=10)
        self.nb.add(f, text="Firmware")
        ttk.Label(f, wraplength=560, justify="left", text=(
            "Update the firmware over USB (no connection with the Connect button is needed).\n"
            "1. Remove the battery and unplug the USB cable.\n"
            "2. Hold the button down (with a pen tip) and plug in the USB cable.\n"
            "3. When the LED starts the bootloader blinking, release the button.\n"
            "4. Choose the firmware file (.hex) and press Update. "
            "When it finishes, unplug and replug the USB cable.")).grid(row=0, column=0, columnspan=3, sticky="w")
        ttk.Label(f, text="Firmware file:").grid(row=1, column=0, sticky="w", pady=(10, 2))
        self.fw_path = tk.StringVar()
        ttk.Entry(f, textvariable=self.fw_path, width=60).grid(row=2, column=0, columnspan=2, sticky="ew")
        ttk.Button(f, text="Browse…", command=self._on_fw_browse).grid(row=2, column=2, padx=6)
        self.fw_btn = ttk.Button(f, text="Update", command=self._on_fw_update)
        self.fw_btn.grid(row=3, column=0, sticky="w", pady=8)
        self.fw_log = tk.Text(f, height=9, width=80, font=("Consolas", 9), state="disabled")
        self.fw_log.grid(row=4, column=0, columnspan=3, sticky="nsew")
        f.columnconfigure(1, weight=1)
        f.rowconfigure(4, weight=1)

    def _on_fw_browse(self):
        path = filedialog.askopenfilename(title="Firmware file", filetypes=[("Intel HEX", "*.hex")])
        if path:
            self.fw_path.set(path)

    def _fw_log_add(self, line):
        self.fw_log.configure(state="normal")
        self.fw_log.insert("end", line + "\n")
        self.fw_log.see("end")
        self.fw_log.configure(state="disabled")

    def _on_fw_update(self):
        path = self.fw_path.get()
        if not path:
            messagebox.showwarning(APP_TITLE, "Choose a firmware file (.hex).")
            return
        try:
            lo, hi, n = cl.check_app_hex(path)
        except (OSError, RuntimeError, ValueError) as e:
            messagebox.showerror(APP_TITLE, f"This file cannot be written.\n\n{e}")
            return
        avr = cl.find_avrdude()
        if not avr:
            messagebox.showerror(APP_TITLE, "avrdude was not found.\n\n"
                                            "Put avrdude.exe and avrdude.conf in the same folder as this tool.")
            return
        if not messagebox.askyesno(APP_TITLE, f"Write this firmware ({n:,} bytes)?\n\n{path}\n\n"
                                              "The C-Logger must be running the bootloader (see the steps)."):
            return
        if self.device.connected:
            self._disconnect("Disconnected for the firmware update.")
        self.fw_log.configure(state="normal")
        self.fw_log.delete("1.0", "end")
        self.fw_log.configure(state="disabled")

        def done(rc):
            if rc == 0:
                self.status_var.set("Firmware updated.")
                messagebox.showinfo(APP_TITLE, "Firmware updated.\nUnplug and replug the USB cable to start it.")
            else:
                self.status_var.set("Firmware update failed.")
                messagebox.showerror(APP_TITLE, f"Firmware update failed (avrdude exit code {rc}).\n\n"
                                                "Check that the C-Logger is running the bootloader.")

        self._run("Updating firmware…", lambda: cl.update_firmware(
            path, avr, output=lambda line: self.q.put(("fwlog", None, line))), done, "indeterminate")

    # ------------------------------------------------------------------
    # 非同期実行
    # ------------------------------------------------------------------
    def _run(self, text, work, done=None, progress_mode=None):
        """work() をワーカースレッドで実行し、結果を done(result) に渡す (UIスレッド)"""
        if self.busy:
            return
        self.busy = True
        self._update_buttons()
        self.status_var.set(text)
        if progress_mode == "indeterminate":
            self.progress.configure(mode="indeterminate")
            self.progress.start(15)

        def worker():
            try:
                r = work()
                self.q.put(("done", done, r))
            except Exception as e:  # noqa: BLE001 (UIに表示する)
                self.q.put(("error", None, e))

        threading.Thread(target=worker, daemon=True).start()

    def _poll_queue(self):
        try:
            while True:
                kind, cb, payload = self.q.get_nowait()
                if kind == "live":
                    self._on_live_result(payload)
                elif kind == "live_error":
                    self._stop_live()
                    self._handle_error(payload)
                elif kind == "fwlog":
                    self._fw_log_add(payload)
                elif kind == "clockprog":
                    el, total, ppm = payload
                    self.progress.configure(mode="determinate", maximum=max(total, 1), value=min(el, total))
                    est = "-" if ppm is None else f"{ppm:+.1f} ppm"
                    self.status_var.set(f"Measuring clock drift… {el / 60:.1f} / {total / 60:.0f} min "
                                        f"(estimate {est})")
                elif kind == "frcprog":
                    i, n, r = payload
                    self.progress.configure(mode="determinate", maximum=n, value=i)
                    self.status_var.set(f"Measuring before recalibration… {i} / {n}  "
                                        f"(CO2 {r['co2_ppm']} ppm before recalibration)")
                elif kind == "progress":
                    n, total = payload
                    self.progress.configure(mode="determinate", maximum=max(total, 1), value=n)
                    self.status_var.set(f"Downloading… {n:,} / {total:,}")
                else:
                    self._task_finished()
                    if kind == "done":
                        if cb:
                            cb(payload)
                    else:
                        self._handle_error(payload)
        except queue.Empty:
            pass
        self.after(50, self._poll_queue)

    def _task_finished(self):
        self.busy = False
        self.clock_stop_btn.configure(state="disabled")
        self.frc_stop_btn.configure(state="disabled")
        self.progress.stop()
        self.progress.configure(mode="determinate", value=0)
        self._update_buttons()

    def _handle_error(self, e):
        # ポートが消えた(USBを抜いた)などの通信異常は切断扱いにする
        if isinstance(e, (serial.SerialException, OSError)) or "no response" in str(e):
            self._disconnect(f"Connection lost: {e}")
            messagebox.showerror(APP_TITLE, f"Connection lost.\n\n{e}")
        else:
            self.status_var.set(f"Error: {e}")
            messagebox.showerror(APP_TITLE, str(e))

    # ------------------------------------------------------------------
    # 接続
    # ------------------------------------------------------------------
    def _refresh_ports(self):
        ports = [p.device for p in list_ports.comports() if p.vid == cl.USB_VID and p.pid == cl.USB_PID]
        self.port_combo["values"] = [AUTO_PORT] + ports
        if self.port_var.get() not in self.port_combo["values"]:
            self.port_var.set(AUTO_PORT)

    def _on_connect(self):
        if self.device.connected:
            self._disconnect("Disconnected.")
            return
        port = self.port_var.get()

        def work():
            p = cl.find_port() if port == AUTO_PORT else port
            if p is None:
                raise RuntimeError("C-Logger not found. Check the USB connection.\n"
                                   "(A C-Logger that is logging does not respond. Re-plug it to stop logging.)")
            self.device.open(p)
            try:
                return self._read_info()
            except Exception:
                self.device.close()
                raise

        self._run("Connecting…", work, self._on_info, "indeterminate")

    def _disconnect(self, msg):
        self._stop_live()
        self.device.close()
        self._set_connected(False)
        self.status_var.set(msg)

    def _set_connected(self, on):
        self.connect_btn.configure(text="Disconnect" if on else "Connect")
        self.port_combo.configure(state="disabled" if on else "readonly")
        if not on:
            for v in self.info_vars.values():
                v.set("-")
            self.info = {}
        self._update_buttons()

    def _update_buttons(self):
        st = "normal" if (self.device.connected and not self.busy) else "disabled"
        for b in self.device_buttons:
            b.configure(state=st)
        # リアルタイム表示の停止はいつでもできるように
        if self.live_on:
            self.live_btn.configure(state="normal")
        apply_ok = st == "normal" and self.clock_new is not None
        self.clock_apply_btn.configure(state="normal" if apply_ok else "disabled")
        if st == "normal" and not self._has_co2():
            self.frc_btn.configure(state="disabled")  # CO2なし版
        self.connect_btn.configure(state="disabled" if self.busy else "normal")

    # ------------------------------------------------------------------
    # 機器情報
    # ------------------------------------------------------------------
    def _read_info(self):
        def f(d):
            return {"hello": d.request("hello"), "id": d.request("get_id").get("device_id"),
                    "status": d.request("get_status"), "battery": d.request("get_battery"),
                    "measure": d.request("measure"),  # センサ構成(CO2あり/なし)の判定に使う
                    "clock": d.request("get_clock_calib"), "corr": d.get_correction()}
        return self.device.call(f)

    def _has_co2(self):
        return self.info.get("measure", {}).get("co2_ppm") is not None

    def _corr(self):
        return self.info.get("corr") or cl.Correction()


    def _refresh_info(self):
        self._run("Reading device information…", self._read_info, self._on_info)

    def _on_info(self, info):
        self.info = info
        self._set_connected(True)
        h, s = info["hello"], info["status"]
        self.info_vars["device"].set(f"{h.get('device')}  FW {h.get('fw')}  ({self.device.port})")
        self.info_vars["id"].set(info["id"])
        self.info_vars["battery"].set(f"{info['battery']['vbat_mV'] / 1000:.2f} V")
        cap_days = s["capacity"] * s["interval"] / 86400
        self.info_vars["records"].set(f"{s['count']:,} / {s['capacity']:,}  ({cap_days:,.0f} days at {s['interval']} s)")
        if s["count"] > 0:
            end = s["start_dt"] + (s["count"] - 1) * s["interval"]
            self.info_vars["period"].set(f"{fmt_time(s['start_dt'])} – {fmt_time(end)}")
        else:
            self.info_vars["period"].set("(no data)")
        self.info_vars["sensor"].set("CO2, temperature, humidity" if self._has_co2() else "Temperature, humidity")
        self._show_corr(info["corr"])
        self.clock_cur.set(f"Current correction: {info['clock']['ppm']:+d} ppm "
                           "(positive = the crystal runs fast and is slowed down)")
        self.interval_var.set(s["interval"])
        self._update_capacity()
        self.status_var.set(f"Connected to {self.device.port}.")

    def _update_capacity(self):
        try:
            sec = int(self.interval_var.get())
        except (tk.TclError, ValueError):
            self.capacity_var.set("")
            return
        cap = self.info.get("status", {}).get("capacity", 2751840)
        self.capacity_var.set(f"Memory lasts about {cap * sec / 86400:,.0f} days at this interval.")

    # ------------------------------------------------------------------
    # 記録
    # ------------------------------------------------------------------
    def _on_start_logging(self):
        try:
            sec = int(self.interval_var.get())
        except (tk.TclError, ValueError):
            sec = 0
        if not 1 <= sec <= INTERVAL_MAX_S:
            messagebox.showwarning(APP_TITLE, f"Recording interval must be 1–{INTERVAL_MAX_S} seconds.")
            return
        n = self.info.get("status", {}).get("count", 0)
        msg = f"Start logging every {sec} s?\n\nThe device clock is set to this PC's time."
        if n > 0:
            msg += f"\n\n{n:,} stored records will be ERASED. Save them first if needed."
        if not messagebox.askyesno(APP_TITLE, msg, icon="warning" if n else "question"):
            return
        self._stop_live()

        def work():
            def f(d):
                d.request("clear_memory", wait_s=300.0)
                d.request("set_interval", {"sec": sec})
                now = int(time.time())
                d.request("set_time", {"epoch": now})
                d.request("start_logging")
                return now
            return self.device.call(f)

        def done(now):
            self._disconnect(f"Logging started at {fmt_time(now)}.")
            messagebox.showinfo(APP_TITLE, (
                f"Logging started at {fmt_time(now)} (every {sec} s).\n\n"
                "The C-Logger has disconnected from USB. You can unplug it now.\n"
                "To stop logging, plug it into USB again or hold the button for 5 seconds."))

        self._run("Erasing memory and starting logging (may take a few minutes)…", work, done, "indeterminate")

    # ------------------------------------------------------------------
    # データ
    # ------------------------------------------------------------------
    def _on_save_csv(self):
        s = self.info.get("status", {})
        if s.get("count", 0) == 0:
            messagebox.showinfo(APP_TITLE, "No stored data.")
            return
        path = filedialog.asksaveasfilename(
            title="Save CSV", defaultextension=".csv", filetypes=[("CSV", "*.csv")],
            initialfile=cl.default_csv_name(self.info.get("id", ""), s.get("start_dt", 0)))
        if not path:
            return
        self._stop_live()

        def work():
            def f(d):
                corr = d.get_correction()
                return corr, d.download(lambda n, total: self.q.put(("progress", None, (n, total))))
            corr, (start_dt, interval, recs) = self.device.call(f)
            cl.write_csv(path, start_dt, interval, recs, corr)
            return len(recs), cl.count_failed(recs)

        def done(r):
            n, nfail = r
            self.status_var.set(f"Saved {n:,} records.")
            messagebox.showinfo(APP_TITLE, f"Saved {n:,} records ({nfail:,} failed measurements) to\n{path}")

        self._run("Downloading…", work, done)

    def _on_erase(self):
        n = self.info.get("status", {}).get("count", 0)
        if not messagebox.askyesno(APP_TITLE, f"Erase all {n:,} stored records?\nThis cannot be undone.",
                                   icon="warning"):
            return
        self._run("Erasing memory (may take a few minutes)…",
                  lambda: self.device.call(lambda d: d.request("clear_memory", wait_s=300.0)),
                  lambda r: self._refresh_info(), "indeterminate")

    # ------------------------------------------------------------------
    # リアルタイム表示
    # ------------------------------------------------------------------
    def _toggle_live(self):
        if self.live_on:
            self._stop_live()
        else:
            self._start_live()

    def _start_live(self):
        if not self.device.connected or self.live_on:
            return
        self.live_on = True
        self.live_btn.configure(text="Stop")

        def loop():
            while self.live_on and self.device.connected:
                try:
                    r = self.device.call(lambda d: d.request("measure"))
                    self.q.put(("live", None, r))
                except Exception as e:  # noqa: BLE001
                    if self.live_on:
                        self.q.put(("live_error", None, e))
                    return
                time.sleep(LIVE_INTERVAL_S)

        self.live_thread = threading.Thread(target=loop, daemon=True)
        self.live_thread.start()
        self._update_buttons()

    def _stop_live(self):
        self.live_on = False
        self.live_btn.configure(text="Start")
        self._update_buttons()

    def _on_live_result(self, r):
        if not self.live_on:
            return
        r = self._corr().apply_measure(r)
        self.live_temp.set(f"{r['temp_C']:6.2f} °C")
        self.live_hum.set(f"{r['hum_pct']:6.2f} %RH")
        co2 = r.get("co2_ppm")
        self.live_co2.set("" if co2 is None else f"{co2:5d} ppm")

    # ------------------------------------------------------------------
    # 時計の補正
    # ------------------------------------------------------------------
    def _on_clock_measure(self):
        minutes = float(self.clock_min.get())
        cur = self.info.get("clock", {}).get("ppm", 0)
        self._stop_live()
        self.clock_new = None
        self.clock_stop_flag = False
        self.clock_stop_btn.configure(state="normal")

        def work():
            return self.device.call(lambda d: d.measure_drift(
                minutes * 60, progress=lambda el, total, ppm: self.q.put(("clockprog", None, (el, total, ppm))),
                stop=lambda: self.clock_stop_flag))

        def done(r):
            ppm, err, n, sec = r
            self.clock_stop_btn.configure(state="disabled")
            self.clock_new = max(-127, min(127, round(cur + ppm)))
            self.clock_result.set(
                f"Drift      : {ppm:+.2f} ppm (±{err:.2f}, {n} points, {sec / 60:.1f} min)\n"
                f"             {ppm * 0.0864:+.2f} s/day with the current correction ({cur:+d} ppm)\n"
                f"Suggested  : {self.clock_new:+d} ppm")
            self.status_var.set(f"Clock drift {ppm:+.2f} ppm.")
            self._update_buttons()

        self._run(f"Measuring clock drift ({minutes:.0f} min)…", work, done)

    def _on_clock_stop(self):
        self.clock_stop_flag = True
        self.clock_stop_btn.configure(state="disabled")

    def _on_clock_apply(self):
        if self.clock_new is not None:
            self._set_clock_calib(self.clock_new)

    def _set_clock_calib(self, ppm):
        def done(r):
            self.clock_new = None
            self.clock_result.set(f"Correction set to {r['ppm']:+d} ppm (saved to the device).")
            self._refresh_info()

        self._run("Setting clock correction…", lambda: self.device.call(
            lambda d: d.request("set_clock_calib", {"ppm": ppm})), done)

    # ------------------------------------------------------------------
    # 補正係数・CO2 の強制校正
    # ------------------------------------------------------------------
    def _show_corr(self, corr):
        for k, (va, vb) in self.corr_vars.items():
            a, b = corr.coefs[k]
            va.set(f"{a:.5f}")
            vb.set(f"{b:.2f}")

    def _on_corr_save(self):
        corr = cl.Correction()
        try:
            for k, (va, vb) in self.corr_vars.items():
                corr.coefs[k] = (float(va.get()), float(vb.get()))
        except ValueError:
            messagebox.showwarning(APP_TITLE, "Enter numbers for a and b.")
            return
        self._save_corr(corr)

    def _on_corr_reset(self):
        if messagebox.askyesno(APP_TITLE, "Reset all coefficients to no correction (a = 1, b = 0)?"):
            self._save_corr(cl.Correction())

    def _save_corr(self, corr):
        def done(saved):
            self.info["corr"] = saved
            self._show_corr(saved)
            self.status_var.set("Correction coefficients saved to the device.")

        self._run("Saving correction coefficients…",
                  lambda: self.device.call(lambda d: d.set_correction(corr)), done)

    def _on_frc(self):
        try:
            ppm = int(self.frc_ppm.get())
        except ValueError:
            ppm = 0
        if not 300 <= ppm <= 5000:
            messagebox.showwarning(APP_TITLE, "Reference CO2 must be 300–5000 ppm.")
            return
        if not messagebox.askyesno(APP_TITLE, (
                f"Recalibrate the CO2 sensor to {ppm} ppm?\n\n"
                "The C-Logger must be in air of this concentration, and the reading must be stable. "
                f"The tool measures {cl.FRC_SAMPLES} times (about a minute) first.")):
            return
        self._stop_live()
        self.frc_stop_flag = False
        self.frc_stop_btn.configure(state="normal")
        self.frc_result.set("")

        def work():
            return self.device.call(lambda d: cl.run_frc(
                d, ppm, progress=lambda i, n, r: self.q.put(("frcprog", None, (i, n, r))),
                stop=lambda: self.frc_stop_flag))

        def done(r):
            if r is None:
                self.frc_result.set("Stopped. The sensor was not recalibrated.")
                self.status_var.set("Recalibration stopped.")
                return
            self.frc_result.set(f"Recalibrated to {r['ppm']} ppm. Correction applied by the sensor: "
                                f"{r['correction']:+d} ppm.")
            self.status_var.set("CO2 sensor recalibrated.")

        self._run("Measuring before recalibration…", work, done)

    def _on_frc_stop(self):
        self.frc_stop_flag = True
        self.frc_stop_btn.configure(state="disabled")

    # ------------------------------------------------------------------
    # 自己診断
    # ------------------------------------------------------------------
    def _on_selftest(self):
        def done(r):
            ok = lambda b: "OK" if b else "NG"  # noqa: E731
            fl, se = r["flash"], r["sensor"]
            meas = cl.fmt_measure(se) if se["ok"] else "-"
            self.selftest_var.set(
                f"Flash   : {ok(fl['ok'] and fl['rw'])}  (JEDEC {fl['jedec']}, read/write {ok(fl['rw'])})\n"
                f"Model   : {'with CO2 sensor' if r.get('co2') else 'temperature/humidity only'}\n"
                f"Sensor  : {ok(r.get('sensor_i2c'))}\n"
                f"Measure : {ok(se['ok'])}  ({meas})\n"
                f"Overall : {ok(r['ok'])}")
            self.status_var.set(f"Self-test: {ok(r['ok'])}")

        self._stop_live()
        self._run("Running self-test…", lambda: self.device.call(
            lambda d: d.request("selftest", wait_s=10.0)), done, "indeterminate")

    # ------------------------------------------------------------------
    def _on_close(self):
        self.live_on = False
        try:
            self.device.close()
        finally:
            self.destroy()


def main():
    app = App()
    app.mainloop()


if __name__ == "__main__":
    main()
