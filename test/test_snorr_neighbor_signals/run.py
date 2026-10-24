"""Test the actual SNNS module/Throttle and collector using host GCC + sanitizers."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / "snns-test"

    def run(source, defines, extra_sources=()):
        subprocess.run(
            [
                "g++",
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-Itest/test_snorr_neighbor_signals/stubs",
                "-Isrc",
                "-Isrc/mesh",
                *defines,
                source,
                *extra_sources,
                "-o",
                str(executable),
            ],
            cwd=ROOT,
            check=True,
        )
        subprocess.run([str(executable)], check=True)

    for fallback in (300, 180):
        print(f"Module: radio fallback {fallback}s", flush=True)
        run(
            "test/test_snorr_neighbor_signals/test_module.cpp",
            [
                "-DSNORR_STANDALONE_MODULE_TEST",
                f"-DUSERPREFS_SNORR_NEIGHBOR_LORA_INTERVAL_SECS={fallback}",
            ],
            ["src/mesh/Throttle.cpp"],
        )
    for capacity in (32, 10):
        print(f"Collector: capacity {capacity}", flush=True)
        run(
            "test/test_snorr_neighbor_signals/test_main.cpp",
            [
                "-DSNORR_STANDALONE_TEST",
                f"-DUSERPREFS_SNORR_NEIGHBOR_CAPACITY={capacity}",
            ],
        )
