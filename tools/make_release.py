#!/usr/bin/env python3
"""Builds the single-file firmware for M5Burner / esptool (flash at 0x0).

Run after `pio run -e cardputer`:
  python tools/make_release.py

Output: release/macrocard-v<version>-cardputer.bin
"""
import re
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
build = root / ".pio" / "build" / "cardputer"
version = re.search(r'MACROCARD_VERSION "([^"]+)"', (root / "src" / "version.h").read_text()).group(1)
boot_app0 = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"

parts = [
    (0x0, build / "bootloader.bin"),
    (0x8000, build / "partitions.bin"),
    (0xE000, boot_app0),
    (0x10000, build / "firmware.bin"),
]
for _, f in parts:
    if not f.exists():
        sys.exit(f"missing {f} - run `pio run -e cardputer` first")

out = root / "release" / f"macrocard-v{version}-cardputer.bin"
out.parent.mkdir(exist_ok=True)
cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin", "-o", str(out),
       "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep"]
for off, f in parts:
    cmd += [hex(off), str(f)]
subprocess.run(cmd, check=True)

merged = out.read_bytes()
for off, f in parts:
    data = f.read_bytes()
    if merged[off:off + len(data)] != data:
        sys.exit(f"verify failed for {f.name} at {hex(off)}")
print(f"OK: {out} ({len(merged)} bytes), flash at 0x0")
