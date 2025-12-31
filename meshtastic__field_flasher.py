import os
import shutil
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font
import ctypes


SETURL_VALUE = r"https://meshtastic.org/e/#CjESIIBUPqT60FhSuxvl2JP2kE7uVHX5ygCrMJ8y8pLb5vXOGgVTTk9SUigBMAE6AgggEhgIARj6ASALKAU4AUAFSAFQHlgjaAHIBgE"
UF2_FILE_PATH = r"rak4631.uf2"


def run_cmd(cmd, log_fn):
    log_fn(f"$ {' '.join(cmd)}\n")
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in p.stdout:
        log_fn(line)
    rc = p.wait()
    if rc != 0:
        raise RuntimeError(f"Command failed (exit {rc})")


# Windows drive type constants
DRIVE_REMOVABLE = 2
GetDriveTypeW = ctypes.windll.kernel32.GetDriveTypeW


def list_removable_drives_windows():
    """
    Returns removable drives like ['E:\\', 'F:\\']
    Uses native Windows API (no WMIC).
    """
    drives = []
    bitmask = ctypes.windll.kernel32.GetLogicalDrives()

    for i in range(26):
        if bitmask & (1 << i):
            drive = f"{chr(65 + i)}:\\"
            dtype = GetDriveTypeW(ctypes.c_wchar_p(drive))
            if dtype == DRIVE_REMOVABLE:
                drives.append(drive)

    return drives


def detect_uf2_drives():
    """
    Detect UF2 drives by presence of UF2 marker files.
    """
    drives = []
    for d in list_removable_drives_windows():
        try:
            if (
                os.path.exists(os.path.join(d, "INFO_UF2.TXT"))
                or os.path.exists(os.path.join(d, "UF2INFO.TXT"))
            ):
                drives.append(d)
        except Exception:
            pass
    return drives


def copy_uf2_to_drive(uf2_path, drive_root, log_fn):
    if not os.path.isfile(uf2_path) or not uf2_path.lower().endswith(".uf2"):
        raise RuntimeError("UF2 file is invalid (must be a .uf2).")
    if not os.path.isdir(drive_root):
        raise RuntimeError(f"Drive not found: {drive_root}")
    dest = os.path.join(drive_root, os.path.basename(uf2_path))
    log_fn(f"Copying UF2 to {dest}\n")
    shutil.copyfile(uf2_path, dest)
    log_fn("Copy complete. Device may reboot.\n")


def wait_seconds(sec, log_fn):
    for i in range(sec, 0, -1):
        log_fn(f"Waiting for reboot... {i}s\n")
        time.sleep(1)
    log_fn("\n")


class App:
    def __init__(self, root):
        self.root = root

        # Touch fonts
        self.touch_font = font.Font(family="Segoe UI", size=18)
        self.touch_font_bold = font.Font(family="Segoe UI", size=18, weight="bold")
        self.log_font = font.Font(family="Consolas", size=12)

        root.title("Meshtastic Field Flasher (Manual Lat/Lon)")
        # Fullscreen (ESC to exit fullscreen)
        root.attributes("-fullscreen", True)
        root.bind("<Escape>", lambda e: root.attributes("-fullscreen", False))


        # ttk style for big buttons
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass
        style.configure("Touch.TButton", font=self.touch_font_bold, padding=(5, 15))
        style.configure("Keypad.TButton", font=self.touch_font_bold, padding=(5, 15))
        

        # State
        self.uf2_path = tk.StringVar(value=UF2_FILE_PATH if os.path.isfile(UF2_FILE_PATH) else "")
        self.uf2_drive = tk.StringVar()
        self.meshtastic_exe = tk.StringVar(value="meshtastic")
        self.device_port = tk.StringVar(value="")

        # Owner split: letters + number
        self.owner_letters = tk.StringVar(value="SNORR TESTRAK")
        self.owner_num = tk.StringVar(value="01")
        self.owner_short_letters = tk.StringVar(value="MT")
        self.owner_short_num = tk.StringVar(value="01")

        self.lat = tk.StringVar(value="37.8651")
        self.lon = tk.StringVar(value="-119.5383")

        # Focus/tab state
        self.active_widget = None
        self.field_widgets = []

        self._build_ui()
        self.refresh_drives()

    # ---------- focus helpers ----------
    def _on_widget_focus(self, w):
        self.active_widget = w
        try:
            if isinstance(w, tk.Entry):
                w.icursor("end")
        except Exception:
            pass

    def _register_field(self, w):
        self.field_widgets.append(w)
        try:
            w.bind("<FocusIn>", lambda ev, ww=w: self._on_widget_focus(ww))
        except Exception:
            pass
        return w

    def _focus_next(self):
        if not self.field_widgets:
            return
        w = self.root.focus_get()
        try:
            idx = self.field_widgets.index(w)
        except Exception:
            idx = -1
        nxt = self.field_widgets[(idx + 1) % len(self.field_widgets)]
        nxt.focus_set()

    def _focus_prev(self):
        if not self.field_widgets:
            return
        w = self.root.focus_get()
        try:
            idx = self.field_widgets.index(w)
        except Exception:
            idx = 0
        prv = self.field_widgets[(idx - 1) % len(self.field_widgets)]
        prv.focus_set()

    # ---------- keypad helpers ----------
    def _keypad_insert(self, ch):
        w = self.active_widget
        if w is None:
            return

        # Only insert into Entries; ignore Combobox, etc.
        if not isinstance(w, tk.Entry):
            return

        if ch == "⌫":
            # delete selection if any, else backspace
            try:
                sel_first = w.index("sel.first")
                sel_last = w.index("sel.last")
                w.delete(sel_first, sel_last)
            except Exception:
                idx = w.index(tk.INSERT)
                if idx > 0:
                    w.delete(idx - 1, idx)
            return

        if ch == "CLR":
            w.delete(0, "end")
            return

        if ch == "±":
            s = w.get()
            if s.startswith("-"):
                w.delete(0, 1)
            else:
                w.insert(0, "-")
            return

        w.insert(tk.INSERT, ch)

    def _increment_owner_number(self):
        def inc(var: tk.StringVar):
            s = var.get().strip()
            width = len(s) if s.isdigit() and len(s) > 0 else 0
            try:
                n = int(s) if s else 0
            except ValueError:
                n = 0
                width = 0
            n += 1
            var.set(str(n).zfill(width) if width > 0 else str(n))

        inc(self.owner_num)
        inc(self.owner_short_num)

    # --- helper: tk.Entry that triggers OSK better on Windows tablets ---
    def touch_entry(self, parent, textvariable, width):
        e = tk.Entry(
            parent,
            textvariable=textvariable,
            width=width,
            font=self.touch_font,
            relief="solid",
            bd=1
        )
        e.bind("<Button-1>", lambda ev: e.focus_set())
        e.bind("<FocusIn>", lambda ev, w=e: self._on_widget_focus(w))
        return e

    def _build_keypad(self, parent):
        kp = ttk.Frame(parent, padding=(10, 8))

        keys = [
            ["7", "8", "9"],
            ["4", "5", "6"],
            ["1", "2", "3"],
            ["0", ".", "-"],
            ["⌫", "CLR", "±"],
        ]

        for r, row in enumerate(keys, start=0):
            for c, k in enumerate(row):
                ttk.Button(
                    kp,
                    text=k,
                    style="Keypad.TButton",
                    command=lambda kk=k: self._keypad_insert(kk),
                    width=4
                ).grid(row=r, column=c, padx=6, pady=6, sticky="nsew")

        nav = ttk.Frame(kp)
        nav.grid(row=len(keys), column=0, columnspan=3, pady=(10, 0), sticky="we")
        nav.columnconfigure(0, weight=1)
        nav.columnconfigure(1, weight=1)
        nav.columnconfigure(2, weight=1)

        ttk.Button(nav, text="Prev", style="Keypad.TButton", command=self._focus_prev)\
            .grid(row=0, column=0, padx=6, pady=6, sticky="we")
        ttk.Button(nav, text="Next", style="Keypad.TButton", command=self._focus_next)\
            .grid(row=0, column=1, padx=6, pady=6, sticky="we")
        ttk.Button(nav, text="++", style="Keypad.TButton", command=self._increment_owner_number)\
            .grid(row=0, column=2, padx=6, pady=6, sticky="we")
        # Add a second row for Exit
        nav.rowconfigure(1, weight=1)

        ttk.Button(nav, text="Exit", style="Keypad.TButton", command=self.root.destroy)\
            .grid(row=1, column=0, columnspan=3, padx=6, pady=(6, 0), sticky="we")

        for c in range(3):
            kp.columnconfigure(c, weight=1)
        for r in range(len(keys) + 1):
            kp.rowconfigure(r, weight=1)

        return kp

    # ---------- UI ----------
    def _build_ui(self):
        # two-column layout: left = form/log, right = keypad
        outer = ttk.Frame(self.root, padding=16)
        outer.pack(fill="both", expand=True)
        outer.columnconfigure(0, weight=2)   # form/log
        outer.columnconfigure(1, weight=1)   # keypad (1/3 width)

        outer.rowconfigure(0, weight=1)

        frm = ttk.Frame(outer)
        frm.grid(row=0, column=0, sticky="nsew")

        keypad = self._build_keypad(outer)
        keypad.grid(row=0, column=1, sticky="ne", padx=(12, 0))

        for i in range(0, 10):
            frm.rowconfigure(i, pad=12)

        r = 0
        ttk.Label(frm, text="UF2 Firmware (.uf2):", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self._register_field(self.touch_entry(frm, self.uf2_path, width=55)).grid(row=r, column=1, sticky="we", padx=10)
        ttk.Button(frm, text="Browse…", command=self.pick_uf2, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="UF2 Device Drive:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self.drive_combo = ttk.Combobox(frm, textvariable=self.uf2_drive, width=10, values=[], font=self.touch_font)
        self.drive_combo.grid(row=r, column=1, sticky="w", padx=10)
        self._register_field(self.drive_combo)
        ttk.Button(frm, text="Refresh Drives", command=self.refresh_drives, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="Meshtastic CLI:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self._register_field(self.touch_entry(frm, self.meshtastic_exe, width=28)).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Device port (optional):", font=self.touch_font).grid(row=r, column=1, sticky="e")
        self._register_field(self.touch_entry(frm, self.device_port, width=10)).grid(row=r, column=2, sticky="w")

        r += 1
        ttk.Label(frm, text="Owner:", font=self.touch_font).grid(row=r, column=0, sticky="w")

        owner_row = ttk.Frame(frm)
        owner_row.grid(row=r, column=1, sticky="w", padx=10)
        self._register_field(self.touch_entry(owner_row, self.owner_letters, width=18)).pack(side="left", padx=(0, 10))
        ttk.Label(owner_row, text="#", font=self.touch_font).pack(side="left", padx=(0, 6))
        self._register_field(self.touch_entry(owner_row, self.owner_num, width=6)).pack(side="left")

        ttk.Label(frm, text="Owner Short:", font=self.touch_font).grid(row=r, column=1, sticky="e")
        short_row = ttk.Frame(frm)
        short_row.grid(row=r, column=2, sticky="w")
        self._register_field(self.touch_entry(short_row, self.owner_short_letters, width=6)).pack(side="left", padx=(0, 10))
        ttk.Label(short_row, text="#", font=self.touch_font).pack(side="left", padx=(0, 6))
        self._register_field(self.touch_entry(short_row, self.owner_short_num, width=6)).pack(side="left")

        r += 1
        ttk.Label(frm, text="Latitude:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self._register_field(self.touch_entry(frm, self.lat, width=16)).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Longitude:", font=self.touch_font).grid(row=r, column=1, sticky="e")
        self._register_field(self.touch_entry(frm, self.lon, width=16)).grid(row=r, column=2, sticky="w")

        r += 1
        btns = ttk.Frame(frm)
        btns.grid(row=r, column=0, columnspan=3, sticky="we", pady=(12, 8))

        ttk.Button(btns, text="FLASH + CONFIGURE", command=self.flash_and_configure, style="Touch.TButton", width=20)\
            .pack(side="left", padx=10, pady=6)
        ttk.Button(btns, text="Configure Only", command=self.configure_only, style="Touch.TButton", width=16)\
            .pack(side="left", padx=10, pady=6)
        ttk.Button(btns, text="Clear Log", command=self.clear_log, style="Touch.TButton", width=12)\
            .pack(side="left", padx=10, pady=6)

        r += 1
        self.log = tk.Text(frm, height=18, wrap="word", font=self.log_font)
        self.log.grid(row=r, column=0, columnspan=3, sticky="nsew", pady=(10, 0))

        frm.columnconfigure(1, weight=1)
        frm.rowconfigure(r, weight=1)

    # ---------- logging ----------
    def log_write(self, s):
        self.log.insert("end", s)
        self.log.see("end")
        self.root.update_idletasks()

    def clear_log(self):
        self.log.delete("1.0", "end")

    # ---------- actions ----------
    def pick_uf2(self):
        p = filedialog.askopenfilename(filetypes=[("UF2 firmware", "*.uf2"), ("All files", "*.*")])
        if p:
            self.uf2_path.set(p)

    def refresh_drives(self):
        drives = detect_uf2_drives()
        self.drive_combo["values"] = drives
        if drives and not self.uf2_drive.get():
            self.uf2_drive.set(drives[0])
        if not drives:
            self.uf2_drive.set("")
            self.log_write("No UF2 drives detected. Put the RAK4631 into UF2 boot mode and click Refresh.\n")

    def build_meshtastic_base(self):
        exe = self.meshtastic_exe.get().strip() or "meshtastic"
        base = [exe]
        port = self.device_port.get().strip()
        if port:
            base += ["--port", port]
        return base

    def validate_inputs(self):
        uf2 = self.uf2_path.get().strip()
        if not uf2 or not os.path.isfile(uf2):
            raise RuntimeError("UF2 firmware file not set or not found.")

        owner_letters = self.owner_letters.get().strip()
        owner_num = self.owner_num.get().strip()
        owner_short_letters = self.owner_short_letters.get().strip()
        owner_short_num = self.owner_short_num.get().strip()

        if not owner_letters or not owner_num or not owner_short_letters or not owner_short_num:
            raise RuntimeError("Owner/Owner Short letters and numbers are required.")

        if not owner_num.isdigit() or not owner_short_num.isdigit():
            raise RuntimeError("Owner numbers must be digits (e.g. 01, 02, 10).")

        try:
            float(self.lat.get().strip())
            float(self.lon.get().strip())
        except ValueError:
            raise RuntimeError("Latitude/Longitude must be valid numbers.")

    def run_meshtastic_config(self):
        self.validate_inputs()

        base = self.build_meshtastic_base()

        owner = f"{self.owner_letters.get().strip()}{self.owner_num.get().strip()}"
        owner_short = f"{self.owner_short_letters.get().strip()}{self.owner_short_num.get().strip()}"
        lat = self.lat.get().strip()
        lon = self.lon.get().strip()

        cmd = (
            base
            + [
                "--seturl", SETURL_VALUE,
                "--set-owner", owner,
                "--set-owner-short", owner_short,
                "--set", "neighbor_info.update_interval", "120",
                "--set", "neighbor_info.transmit_over_lora", "true",
                "--set", "neighbor_info.enabled", "true",
                "--set", "device.role", "ROUTER",
                "--set", "device.rebroadcast_mode", "ALL",
                "--set", "lora.config_ok_to_mqtt", "true",
                "--set", "position.fixed_position", "true",
                "--setlat", lat,
                "--setlon", lon,
            ]
        )

        run_cmd(cmd, self.log_write)

    def configure_only(self):
        def worker():
            try:
                self.log_write("Running Meshtastic configuration...\n")
                self.run_meshtastic_config()
                self.log_write("Configuration complete.\n")
            except Exception as e:
                self.log_write(f"ERROR: {e}\n")
                messagebox.showerror("Error", str(e))
        threading.Thread(target=worker, daemon=True).start()

    def flash_and_configure(self):
        def worker():
            try:
                self.validate_inputs()

                uf2 = self.uf2_path.get().strip()
                drive = self.uf2_drive.get().strip()
                if not drive:
                    raise RuntimeError("Select/detect the UF2 device drive.")

                self.log_write("Flashing UF2...\n")
                copy_uf2_to_drive(uf2, drive, self.log_write)

                wait_seconds(8, self.log_write)

                self.log_write("Running Meshtastic configuration...\n")
                self.run_meshtastic_config()
                self.log_write("FLASH + CONFIGURE complete.\n")
            except Exception as e:
                self.log_write(f"ERROR: {e}\n")
                messagebox.showerror("Error", str(e))
        threading.Thread(target=worker, daemon=True).start()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
