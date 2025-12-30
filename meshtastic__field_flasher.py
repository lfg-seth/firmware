import os
import shutil
import subprocess
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, font

SETURL_VALUE = r"https://meshtastic.org/e/#CjESIIBUPqT60FhSuxvl2JP2kE7uVHX5ygCrMJ8y8pLb5vXOGgVTTk9SUigBMAE6AgggEhgIARj6ASALKAU4AUAFSAFQHlgjaAHIBgE"
UF2_FILE_PATH = r"E:\firmware\.pio\build\rak4631\firmware-rak4631-2.7.17.280b7d773.uf2"


def run_cmd(cmd, log_fn):
    log_fn(f"$ {' '.join(cmd)}\n")
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in p.stdout:
        log_fn(line)
    rc = p.wait()
    if rc != 0:
        raise RuntimeError(f"Command failed (exit {rc})")


def list_removable_drives_windows():
    """Return removable drives like ['E:\\', 'F:\\'] using WMIC."""
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
    """Detect UF2 drives by marker files."""
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
        log_fn(f"Waiting for reboot... {i}s\r")
        time.sleep(1)
    log_fn("\n")


class App:
    def __init__(self, root):
        self.root = root

        # --- Touch-friendly theming ---
        self.touch_font = font.Font(family="Segoe UI", size=16)
        self.touch_font_bold = font.Font(family="Segoe UI", size=16, weight="bold")
        self.log_font = font.Font(family="Consolas", size=12)

        root.option_add("*Font", self.touch_font)

        style = ttk.Style()
        # For some Windows themes, setting theme helps ttk respect padding better
        try:
            style.theme_use("clam")
        except Exception:
            pass

        style.configure("Touch.TLabel", font=self.touch_font)
        style.configure("Touch.TEntry", font=self.touch_font, padding=(10, 10))
        style.configure("Touch.TCombobox", font=self.touch_font, padding=(10, 10))
        style.configure("Touch.TButton", font=self.touch_font_bold, padding=(24, 16))

        root.title("Meshtastic Field Flasher (Manual Lat/Lon)")
        root.geometry("1100x760")

        # Defaults / State
        self.uf2_path = tk.StringVar(value=UF2_FILE_PATH if os.path.isfile(UF2_FILE_PATH) else "")
        self.uf2_drive = tk.StringVar()

        self.meshtastic_exe = tk.StringVar(value="meshtastic")  # in PATH
        self.device_port = tk.StringVar(value="")              # optional, e.g. COM7

        self.owner = tk.StringVar(value="SNORR TESTRAK01")
        self.owner_short = tk.StringVar(value="MT01")
        self.lat = tk.StringVar(value="37.8651")
        self.lon = tk.StringVar(value="-119.5383")

        self._build_ui()
        self.refresh_drives()

    def _build_ui(self):
        frm = ttk.Frame(self.root, padding=16)
        frm.pack(fill="both", expand=True)

        # Grid spacing for fat fingers
        for i in range(0, 10):
            frm.rowconfigure(i, pad=10)

        r = 0
        ttk.Label(frm, text="UF2 Firmware (.uf2):", style="Touch.TLabel").grid(row=r, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.uf2_path, style="Touch.TEntry", width=55).grid(row=r, column=1, sticky="we", padx=10)
        ttk.Button(frm, text="Browse…", command=self.pick_uf2, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="UF2 Device Drive:", style="Touch.TLabel").grid(row=r, column=0, sticky="w")
        self.drive_combo = ttk.Combobox(frm, textvariable=self.uf2_drive, width=12, values=[], style="Touch.TCombobox")
        self.drive_combo.grid(row=r, column=1, sticky="w", padx=10)
        ttk.Button(frm, text="Refresh Drives", command=self.refresh_drives, style="Touch.TButton", width=12).grid(row=r, column=2, sticky="we")

        r += 1
        ttk.Label(frm, text="Meshtastic CLI:", style="Touch.TLabel").grid(row=r, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.meshtastic_exe, style="Touch.TEntry", width=30).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Device port (optional):", style="Touch.TLabel").grid(row=r, column=1, sticky="e")
        ttk.Entry(frm, textvariable=self.device_port, style="Touch.TEntry", width=12).grid(row=r, column=2, sticky="w")

        r += 1
        ttk.Label(frm, text="Owner (long):", style="Touch.TLabel").grid(row=r, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.owner, style="Touch.TEntry", width=30).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Owner (short):", style="Touch.TLabel").grid(row=r, column=1, sticky="e")
        ttk.Entry(frm, textvariable=self.owner_short, style="Touch.TEntry", width=12).grid(row=r, column=2, sticky="w")

        r += 1
        ttk.Label(frm, text="Latitude:", style="Touch.TLabel").grid(row=r, column=0, sticky="w")
        ttk.Entry(frm, textvariable=self.lat, style="Touch.TEntry", width=16).grid(row=r, column=1, sticky="w", padx=10)
        ttk.Label(frm, text="Longitude:", style="Touch.TLabel").grid(row=r, column=1, sticky="e")
        ttk.Entry(frm, textvariable=self.lon, style="Touch.TEntry", width=16).grid(row=r, column=2, sticky="w")

        r += 1
        btns = ttk.Frame(frm)
        btns.grid(row=r, column=0, columnspan=3, sticky="we", pady=(12, 8))

        ttk.Button(
            btns,
            text="FLASH + CONFIGURE",
            command=self.flash_and_configure,
            style="Touch.TButton",
            width=20
        ).pack(side="left", padx=10, pady=6)

        ttk.Button(
            btns,
            text="Configure Only",
            command=self.configure_only,
            style="Touch.TButton",
            width=16
        ).pack(side="left", padx=10, pady=6)

        ttk.Button(
            btns,
            text="Clear Log",
            command=self.clear_log,
            style="Touch.TButton",
            width=12
        ).pack(side="left", padx=10, pady=6)

        r += 1
        self.log = tk.Text(frm, height=20, wrap="word", font=self.log_font)
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

        lat_s = self.lat.get().strip()
        lon_s = self.lon.get().strip()
        try:
            float(lat_s)
            float(lon_s)
        except ValueError:
            raise RuntimeError("Latitude/Longitude must be valid numbers.")

    def run_meshtastic_config(self):
        self.validate_inputs()

        base = self.build_meshtastic_base()
        owner = self.owner.get().strip()
        owner_short = self.owner_short.get().strip()
        lat = self.lat.get().strip()
        lon = self.lon.get().strip()

        commands = [
            base + ["--seturl", SETURL_VALUE],
            base + ["--set-owner", owner],
            base + ["--set-owner-short", owner_short],
            base + ["--set", "neighbor_info.update_interval", "120"],
            base + ["--set", "neighbor_info.transmit_over_lora", "true"],
            base + ["--set", "neighbor_info.enabled", "true"],
            base + ["--set", "device.role", "ROUTER"],
            base + ["--set", "device.rebroadcast_mode", "ALL"],
            base + ["--set", "lora.config_ok_to_mqtt", "true"],
            base + ["--set", "position.fixed_position", "true", "--setlat", lat, "--setlon", lon],
        ]

        for cmd in commands:
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
