import os
import shutil
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font

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


def list_removable_drives_windows():
    try:
        out = subprocess.check_output(
            ["wmic", "logicaldisk", "where", "drivetype=2", "get", "deviceid"],
            text=True,
            stderr=subprocess.DEVNULL
        )
        drives = []
        for line in out.splitlines():
            line = line.strip()
            if line and line.upper().endswith(":"):
                drives.append(line.upper() + "\\")
        return drives
    except Exception:
        return []


def detect_uf2_drives():
    drives = []
    for d in list_removable_drives_windows():
        try:
            if os.path.exists(os.path.join(d, "INFO_UF2.TXT")) or os.path.exists(os.path.join(d, "UF2INFO.TXT")):
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
        root.geometry("1100x780")

        # ttk style for big buttons
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except Exception:
            pass
        style.configure("Touch.TButton", font=self.touch_font_bold, padding=(28, 18))

        # State
        self.uf2_path = tk.StringVar(value=UF2_FILE_PATH if os.path.isfile(UF2_FILE_PATH) else "")
        self.uf2_drive = tk.StringVar()
        self.meshtastic_exe = tk.StringVar(value="meshtastic")
        self.device_port = tk.StringVar(value="")

        self.owner = tk.StringVar(value="SNORR TESTRAK01")
        self.owner_short = tk.StringVar(value="MT01")
        self.lat = tk.StringVar(value="37.8651")
        self.lon = tk.StringVar(value="-119.5383")

        self._build_ui()
        self.refresh_drives()

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
        # Ensure focus is set on tap/click (helps OSK)
        e.bind("<Button-1>", lambda ev: e.focus_set())
        e.bind("<FocusIn>", lambda ev: e.icursor("end"))
        return e

    def _build_ui(self):
        frm = ttk.Frame(self.root, padding=16)
        frm.pack(fill="both", expand=True)

        for i in range(0, 10):
            frm.rowconfigure(i, pad=12)

        r = 0
        ttk.Label(frm, text="UF2 Firmware (.uf2):", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self.touch_entry(frm, self.uf2_path, width=55).grid(row=r, column=1, sticky="we", padx=10)
        ttk.Button(frm, text="Browse…", command=self.pick_uf2, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="UF2 Device Drive:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        # Combobox can also fail OSK; but this field isn't typed often. Keep ttk combobox.
        self.drive_combo = ttk.Combobox(frm, textvariable=self.uf2_drive, width=10, values=[], font=self.touch_font)
        self.drive_combo.grid(row=r, column=1, sticky="w", padx=10)
        ttk.Button(frm, text="Refresh Drives", command=self.refresh_drives, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="Meshtastic CLI:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self.touch_entry(frm, self.meshtastic_exe, width=28).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Device port (optional):", font=self.touch_font).grid(row=r, column=1, sticky="e")
        self.touch_entry(frm, self.device_port, width=10).grid(row=r, column=2, sticky="w")

        r += 1
        ttk.Label(frm, text="Owner (long):", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self.touch_entry(frm, self.owner, width=28).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Owner (short):", font=self.touch_font).grid(row=r, column=1, sticky="e")
        self.touch_entry(frm, self.owner_short, width=10).grid(row=r, column=2, sticky="w")

        r += 1
        ttk.Label(frm, text="Latitude:", font=self.touch_font).grid(row=r, column=0, sticky="w")
        self.touch_entry(frm, self.lat, width=16).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Longitude:", font=self.touch_font).grid(row=r, column=1, sticky="e")
        self.touch_entry(frm, self.lon, width=16).grid(row=r, column=2, sticky="w")

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

    def log_write(self, s):
        self.log.insert("end", s)
        self.log.see("end")
        self.root.update_idletasks()

    def clear_log(self):
        self.log.delete("1.0", "end")

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

        owner = self.owner.get().strip()
        owner_short = self.owner_short.get().strip()
        if not owner or not owner_short:
            raise RuntimeError("Owner and Owner Short are required.")

        try:
            float(self.lat.get().strip())
            float(self.lon.get().strip())
        except ValueError:
            raise RuntimeError("Latitude/Longitude must be valid numbers.")

    def run_meshtastic_config(self):
        self.validate_inputs()

        base = self.build_meshtastic_base()

        owner = self.owner.get().strip()
        owner_short = self.owner_short.get().strip()
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
