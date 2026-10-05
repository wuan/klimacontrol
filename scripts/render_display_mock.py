#!/usr/bin/env python3
"""Render pixel-exact mock PNGs of the e-paper panel layout.

Compiles the real src/display/EPaperDisplay.cpp against the real Adafruit_GFX
library — the same FreeSans*7b.h font headers the firmware uses — with small
host-side shims for the Arduino/GxEPD2 APIs (scripts/render_host/), runs it to
paint each sample screen into a framebuffer, and converts the dumped frames
(PBM) to PNG. What you see is the firmware's own drawing code, bit for bit;
only the panel hardware (SPI, BUSY, page flush) is emulated away.

Requires the PlatformIO library dependencies to be fetched once
(`pio pkg install` or any `pio run`) so the Adafruit GFX sources are present
under .pio/libdeps/, plus Pillow for the PBM → PNG conversion.

Usage:
    python3 scripts/render_display_mock.py [--out DIR]

Outputs one PNG per sample state (normal, each warning token, etc.).
"""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

REPO_ROOT = Path(__file__).resolve().parent.parent
HOST_DIR = REPO_ROOT / "scripts" / "render_host"
SHIM_DIR = HOST_DIR / "shim"
SRC_DIR = REPO_ROOT / "src"
GFX_LIB = REPO_ROOT / ".pio" / "libdeps" / "adafruit_qtpy_esp32s2" / "Adafruit GFX Library"
DEFAULT_OUT = REPO_ROOT / "build" / "mock_display"


def firmware_version() -> str:
    """Same chain as scripts/get_version.py so the header band matches."""
    for args in (["git", "describe", "--tags", "--exact-match"], ["git", "describe", "--tags", "--always"]):
        try:
            return subprocess.check_output(args, cwd=REPO_ROOT, stderr=subprocess.DEVNULL).decode().strip()
        except (subprocess.CalledProcessError, FileNotFoundError):
            continue
    return "v0.0.0-dev"


def compiler() -> str:
    for cc in ("g++", "c++", "clang++"):
        if shutil.which(cc):
            return cc
    sys.exit("No C++ compiler found (need g++, c++ or clang++ on PATH)")


def build(out_dir: Path) -> Path:
    if not GFX_LIB.is_dir():
        sys.exit(f"Adafruit GFX Library not found at {GFX_LIB}.\n"
                 "Run `pio pkg install` (or any `pio run`) first to fetch the "
                 "PlatformIO library dependencies.")

    binary = out_dir / "host" / "render_host"
    binary.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        compiler(),
        "-std=gnu++17",
        "-DARDUINO=10607",
        f'-DFIRMWARE_VERSION="{firmware_version()}"',
        "-Wno-macro-redefined",  # glcdfont.c redefines PROGMEM our shim set
        f"-I{SHIM_DIR}",
        f"-I{SRC_DIR}",
        f"-I{GFX_LIB}",
        str(HOST_DIR / "main.cpp"),
        str(SRC_DIR / "display" / "EPaperDisplay.cpp"),
        str(GFX_LIB / "Adafruit_GFX.cpp"),
        "-o",
        str(binary),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit("Host renderer build failed")
    return binary


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="PNG output directory")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    binary = build(args.out)

    subprocess.run([str(binary), str(args.out)], check=True)

    for pbm in sorted(args.out.glob("display_*.pbm")):
        img = Image.open(pbm).convert("L")
        png = pbm.with_suffix(".png")
        img.save(png)
        pbm.unlink()
        print(f"wrote {png}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
