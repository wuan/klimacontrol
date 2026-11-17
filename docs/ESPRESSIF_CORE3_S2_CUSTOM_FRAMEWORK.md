# Core 3.x on the ESP32-S2: Building a Trimmed Custom Arduino Framework

**Status:** implementation guide for the "keep the ESP32-S2" path.
**Baseline:** PlatformIO `espressif32@8.0.0`, Arduino core 3.3.12, ESP-IDF 5.5.5, GCC 14.2.
**Related:** `openspec/changes/upgrade-espressif32-platform/design.md` (DRAM analysis and
the source-compatibility work already on `espressif32_8_migration`).

---

## 1. TL;DR

The firmware sources already compile on Arduino core 3.3.12. Keeping the
Adafruit QT Py ESP32-S2 requires rebuilding the **precompiled ESP-IDF library
package** (`framework-arduinoespressif32-esp32s2-libs`) with every TinyUSB
class this firmware does not use turned off.

The stock 3.3.12 `libarduino_tinyusb.a` contributes **30,935 bytes** of static
BSS/data. Klima-Control uses only USB CDC serial. Removing the unused device
classes and the unused USB host stack recovers about **29.4 KB**, which is
enough to clear the linker's `dram0_0_seg` overflow (6,184 bytes) and leave
roughly **23.2 KB** of internal DRAM for the runtime heap — close to the
core-2 baseline of 24.6 KB.

This is the "custom framework" option from the upgrade design. It is more work
than moving to an ESP32-S3 board, and it creates a framework fork to maintain.
It is the only way to keep the S2 *and* preserve the current 16 KB low-heap
guard without a spec change.

---

## 2. What is actually being rebuilt

PlatformIO's Arduino builder expects two packages:

| Package | Contents | We change it? |
|---|---|---|
| `framework-arduinoespressif32` | Arduino core sources: `cores/`, `libraries/`, `variants/`, `tools/platformio-build.py` | **No** |
| `framework-arduinoespressif32-esp32s2-libs` | Precompiled ESP-IDF libraries, headers, linker scripts, `sdkconfig`, `flags/` | **Yes** |

The second package is the output of
[`espressif/esp32-arduino-lib-builder`](https://github.com/espressif/esp32-arduino-lib-builder).
Its `idf-libs` target builds ESP-IDF with a set of `sdkconfig` defaults, then
`tools/copy-libs.sh` copies the resulting `lib/`, `include/`, `ld/`, `flags/`,
and `sdkconfig` into the package folder.

The relevant source of the TinyUSB configuration is:

- `components/arduino_tinyusb/Kconfig.projbuild` — the class switches.
- `components/arduino_tinyusb/include/tusb_config.h` — maps the Kconfig
  switches to TinyUSB's `CFG_TUD_*` / `CFG_TUH_*` macros.

The stock `defconfig.common` only sets `CONFIG_TINYUSB_CDC_MAX_PORTS=2`. Every
TinyUSB class defaults to `y`, which is why the shipped archive contains NCM,
MSC, HID, MIDI, audio, video, DFU, vendor, and USB-host code.

---

## 3. The memory problem, measured

At the link step on 3.3.12 the failure is exactly:

```text
region `dram0_0_seg' overflowed by 6184 bytes
```

The stock 3.3.12 `libarduino_tinyusb.a` static BSS/data breakdown (measured
with `xtensa-esp-elf-nm -S --size-sort`):

| Object | BSS/data | Largest buffer |
|---|---:|---:|
| `ncm_device.c` | 6,488 | `ncm_epbuf` 6,416 |
| `audio_device.c` | 6,300 | `ep_out_sw_buf` / `ep_in_sw_buf` 3,104 each |
| `msc_device.c` | 4,160 | `_mscd_epbuf` 4,096 |
| `dfu_device.c` | 4,106 | `_transfer_buf` 4,096 |
| `hid_host.c` | 2,983 | `_hidh_epbuf` 2,688 |
| `usbh.c` | 1,809 | `_usbh_devices` 837 |
| `msc_host.c` | 700 | `_msch_epbuf` 336 |
| `hcd_dwc2.c` | 553 | `_hcd_data` 544 |
| `cdc_host.c` | 548 | `cdch_data` 412 |
| `midi_host.c` | 530 | `_midi_host` 400 |
| `midi_device.c` | 392 | `_midid_itf` 392 |
| `vendor_device.c` | 380 | `_vendord_itf` 380 |
| `hid_device.c` | 204 | — |
| `video_device.c` | 153 | — |
| `hub.c` | 54 | — |
| **Total** | **30,935** | |

This firmware uses only the CDC device class (the USB serial console). It does
not use mass storage, HID, MIDI, audio, video, DFU, vendor, NCM, or USB host.
The Arduino core's own `USBMSC`, `USBHID`, and `USBHost` libraries are not
referenced by the project.

### Expected result after trimming

Keep the minimum CDC device stack:

- `cdc_device.c` 776
- `usbd.c` 543
- `dcd_dwc2.c` 248
- `tusb.c` 8

≈ **1,575 bytes**

Remove:

- unused device classes: **22,183 bytes**
- USB host stack: **7,177 bytes**
- total removed: **29,360 bytes**

After the 6,184-byte link overflow, that leaves about:

```text
29,360 - 6,184 = 23,176 bytes of free internal DRAM
```

The core-2 baseline recorded in the design is 24,608 bytes. The custom
framework is therefore about **1.4 KB below the old baseline**; that is usually
enough to keep the 16 KB runtime guard, but it must be measured on hardware
(see §9). If you want the full baseline back, move `RequestDiag`'s 2,496-byte
ring to PSRAM or trim another small application buffer.

> **Do not skip the USB-host removal.** With only the device classes disabled,
> the recovery is 22,183 bytes and the post-link *static* headroom is 15,999
> bytes — already below the 16 KB `LowHeapGuard` threshold before WiFi/lwIP
> allocate anything, so the device would restart-loop. Removing the host stack
> is what makes this plan viable.

---

## 4. Reproducing the stock 3.3.12 toolchain

Use the exact builder revision that produced the installed 3.3.12 package. The
installed `versions.txt` records:

```text
lib-builder: master 6671d0b
esp-idf: v5.5.5 b774170ff46
arduino: master e7c1e8cd4
tinyusb: master 88c7f0b81
```

Pin the builder to `6671d0bd65cdb9d4cc1001b759e8610de945a8d5` (the full SHA
behind `6671d0b`), and pin the components to:

| Component | Repository | Commit |
|---|---|---|
| Arduino core | `espressif/arduino-esp32` | `e7c1e8cd47bcaf02da3a00d81b886d057d93c7d9` |
| ESP-IDF | `espressif/esp-idf` | `b774170ff46c393eeb5e495ea37936038d3f4f4f` (v5.5.5) |
| TinyUSB | `hathach/tinyusb` | `88c7f0b812ad516adc670bf67627911021f0951e` |

The builder's `build.sh` installs `arduino-esp32` and ESP-IDF from branches
when it starts. For a reproducible build, prepare the components once, check
out the pinned commits, and then build with `-s` (skip install/update).

### Prerequisites

Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y \
  git wget curl libssl-dev libncurses-dev flex bison gperf \
  python3 python3-pip cmake ninja-build ccache jq
python3 -m pip install --user \
  setuptools pyserial click cryptography future pyparsing pyelftools
```

macOS (the lib builder requires GNU sed/awk and GNU realpath):

```bash
brew install git wget curl cmake ninja ccache jq coreutils gnu-sed gawk python3
```

The builder's `tools/config.sh` checks for `gsed`, `gawk`, and `grealpath` on
macOS and fails if they are missing.

Disk and time: the ESP-IDF clone plus one S2 `idf-libs` build is several GB and
typically tens of minutes; the first run is dominated by cloning and installing
the IDF toolchains.

---

## 5. Create the trimmed configuration

### 5.1 Custom defconfig

Create a new file in the builder, for example
`configs/defconfig.tinyusb_cdc_only`:

```text
# Klima-Control: keep USB CDC serial, drop every unused TinyUSB class.
# These are appended after defconfig.common / defconfig.esp32s2, so they
# override the Kconfig defaults.

# CONFIG_TINYUSB_MSC_ENABLED is not set
# CONFIG_TINYUSB_HID_ENABLED is not set
# CONFIG_TINYUSB_MIDI_ENABLED is not set
# CONFIG_TINYUSB_AUDIO_ENABLED is not set
# CONFIG_TINYUSB_VIDEO_ENABLED is not set
# CONFIG_TINYUSB_DFU_RT_ENABLED is not set
# CONFIG_TINYUSB_DFU_ENABLED is not set
# CONFIG_TINYUSB_VENDOR_ENABLED is not set
# CONFIG_TINYUSB_NCM_ENABLED is not set
```

`CONFIG_TINYUSB_ENABLED` and `CONFIG_TINYUSB_CDC_ENABLED` stay at their
default `y`. The CDC maximum-port setting from `defconfig.common` stays at 2;
setting it to 1 saves a few hundred bytes but is not required.

If you want to confirm what the Kconfig names do, see
`components/arduino_tinyusb/Kconfig.projbuild` and the mapping in
`components/arduino_tinyusb/include/tusb_config.h`:

```c
#define CFG_TUD_MSC           CONFIG_TINYUSB_MSC_ENABLED
#define CFG_TUD_HID           CONFIG_TINYUSB_HID_ENABLED
#define CFG_TUD_NCM           CONFIG_TINYUSB_NCM_ENABLED
/* ... */
```

Setting a class to `n` makes its `CFG_TUD_*` macro `0`, and TinyUSB compiles
the class's buffers out.

### 5.2 Disable the USB host stack

The host stack is not controlled by a Kconfig switch. `tusb_config.h` hardcodes:

```c
#define CFG_TUH_ENABLED             CFG_TUD_ENABLED
```

Patch it in the builder:

```diff
--- a/components/arduino_tinyusb/include/tusb_config.h
+++ b/components/arduino_tinyusb/include/tusb_config.h
@@
-#define CFG_TUH_ENABLED             CFG_TUD_ENABLED
+#define CFG_TUH_ENABLED             0  /* Klima-Control: USB host is unused */
```

This removes `usbh.c`, `hcd_dwc2.c`, `hub.c`, the CDC/HID/MSC/MIDI host
drivers, and their buffers. The Arduino core guards its host code with
`#if CFG_TUH_ENABLED`, so setting it to 0 is supported.

Keep this patch in the repository (for example under
`tools/core3/patches/`) so the build is reproducible.

---

## 6. Build the custom libraries

### 6.1 Clone the builder and pin it

```bash
git clone https://github.com/espressif/esp32-arduino-lib-builder.git
cd esp32-arduino-lib-builder
git checkout 6671d0bd65cdb9d4cc1001b759e8610de945a8d5
```

Verify the checkout before continuing:

```bash
git rev-parse HEAD
# 6671d0bd65cdb9d4cc1001b759e8610de945a8d5
```

### 6.2 Install components and pin them

The first install clones the current branches. Do it once, then check out the
3.3.12 commits:

```bash
./tools/update-components.sh
./tools/install-arduino.sh
source ./tools/install-esp-idf.sh

git -C components/arduino checkout e7c1e8cd47bcaf02da3a00d81b886d057d93c7d9
git -C esp-idf checkout b774170ff46c393eeb5e495ea37936038d3f4f4f
git -C esp-idf submodule update --init --recursive

# update-components.sh leaves TinyUSB at master with the builder's patches
# applied. Checking out the pinned commit discards those patches, so re-apply
# the same loop it uses:
git -C components/arduino_tinyusb/tinyusb checkout 88c7f0b812ad516adc670bf67627911021f0951e
for patch in patches/tinyusb/*.diff; do
    [ -e "$patch" ] || continue
    if git -C components/arduino_tinyusb/tinyusb apply --reverse --check "$patch" 2>/dev/null; then
        echo "Skipping $(basename "$patch"), already present"
        continue
    fi
    git -C components/arduino_tinyusb/tinyusb apply "$patch" || exit 1
done

# Make sure the toolchains for the pinned IDF commit are installed,
# then put idf.py and the Xtensa toolchain on PATH.
./esp-idf/install.sh
source ./esp-idf/export.sh
```

If your shell already has a complete ESP-IDF 5.5.5 environment, you can skip
the install steps and just source `export.sh`.

### 6.3 Build only the ESP32-S2 libraries

The single-target `idf-libs` path does *not* automatically apply the
`features` / `idf_libs` arrays from `configs/builds.json`; pass them explicitly.
The stock recipe for S2 is:

```text
configs/defconfig.common
configs/defconfig.esp32s2
configs/defconfig.debug_default
configs/defconfig.qio_ram  # feature
configs/defconfig.qio      # flash mode
configs/defconfig.80m      # flash / PSRAM speed
```

Add the custom config last so it overrides the defaults:

```bash
./build.sh -s -t esp32s2 -b idf-libs \
  qio_ram qio 80m tinyusb_cdc_only
```

The output is written to:

```text
out/tools/esp32-arduino-libs/esp32s2/
├── flags/
├── include/
├── ld/
├── lib/
├── qio_qspi/
│   ├── include/sdkconfig.h
│   └── libspi_flash.a
├── sdkconfig
├── dependencies.lock
└── pioarduino-build.py
```

The exact memory-variant directory name is
`<flash_mode>_<psram_type>`; for the QT Py board it is `qio_qspi`.

### 6.4 Verify the trim

Check the generated `sdkconfig`:

```bash
OUT=out/tools/esp32-arduino-libs/esp32s2

grep -E 'CONFIG_TINYUSB_(CDC|MSC|HID|MIDI|AUDIO|VIDEO|DFU|VENDOR|NCM)' "$OUT/sdkconfig"
```

Expected: `CONFIG_TINYUSB_CDC_ENABLED=y` (or the CDC max-port line), and the
unused classes either absent or shown as `# ... is not set`.

The Arduino core actually compiles against the memory-variant header
(`qio_qspi/include/sdkconfig.h`), so check that one too:

```bash
grep -E 'CONFIG_TINYUSB_(CDC|MSC|HID|MIDI|AUDIO|VIDEO|DFU|VENDOR|NCM)' \
  "$OUT/qio_qspi/include/sdkconfig.h"
```

Both files must agree; otherwise the core would still see the stock class
settings even though the archive was rebuilt.

Check that the big buffers are gone from the archive:

```bash
NM=$(command -v xtensa-esp-elf-nm || \
     find "$HOME/.platformio/packages" \
       -path '*toolchain-xtensa-esp-elf*/bin/xtensa-esp-elf-nm' | head -1)
"$NM" -S --size-sort "$OUT/lib/libarduino_tinyusb.a" \
  | grep -E 'ncm_epbuf|mscd_epbuf|_transfer_buf|ep_in_sw_buf|ep_out_sw_buf|hidh_epbuf'
```

Expected: no output, or only the CDC/usbd buffers you intentionally kept.

A quick total (portable across macOS and Linux):

```bash
"$NM" -S "$OUT/lib/libarduino_tinyusb.a" | python3 -c '
import sys
total = 0
for line in sys.stdin:
    parts = line.split()
    if len(parts) >= 4 and parts[2] in "bBdD":
        total += int(parts[1], 16)
print(total)
'
```

The stock archive is 30,935 bytes; the trimmed archive should be roughly
1.5–2 KB.

---

## 7. Package it for PlatformIO

PlatformIO's `tools/platformio-build.py` expects a package named
`framework-arduinoespressif32-esp32s2-libs` with this layout:

```text
framework-arduinoespressif32-esp32s2-libs/
├── package.json
├── bin/                  # bootloader ELFs (bootloader_qio_80m.elf, ...)
├── flags/
├── include/
├── ld/
├── lib/
├── qio_qspi/
│   ├── include/sdkconfig.h
│   └── libspi_flash.a
├── dio_qspi/             # optional, for dio flash builds
│   ├── include/sdkconfig.h
│   └── libspi_flash.a
├── sdkconfig
└── versions.txt
```

### 7.1 Create the package directory

Use a stable path outside `.pio` (for local development) or a release artifact
path (for CI):

```bash
OUT=out/tools/esp32-arduino-libs/esp32s2
PKG="$HOME/klimacontrol-core3-libs"

rm -rf "$PKG"
mkdir -p "$PKG"
cp -R "$OUT"/{flags,include,ld,lib,sdkconfig,qio_qspi} "$PKG"/
cp out/tools/esp32-arduino-libs/versions.txt "$PKG"/ 2>/dev/null || true
```

### 7.2 Bootloaders

The bootloader is built from ESP-IDF's bootloader component and does not
include TinyUSB; a TinyUSB-only Kconfig change cannot affect it. You can copy
the bootloader ELFs from the stock package before switching PlatformIO over:

```bash
STOCK="$HOME/.platformio/packages/framework-arduinoespressif32-esp32s2-libs"
cp -R "$STOCK/bin" "$PKG"/
```

Alternatively, build them with the builder:

```bash
./build.sh -s -t esp32s2 -b copy-bootloader qio 80m
# and, if you need the other boot modes:
./build.sh -s -t esp32s2 -b copy-bootloader dio 80m
./build.sh -s -t esp32s2 -b copy-bootloader qio 40m
./build.sh -s -t esp32s2 -b copy-bootloader dio 40m
```

The QT Py board uses flash mode `qio` and 80 MHz, so
`bootloader_qio_80m.elf` is the one PlatformIO actually selects.

### 7.3 Optional second memory variant

The QT Py board uses `build.arduino.memory_type = qio_qspi`. `qio_qspi` is
therefore the only required variant. If you want the package to be a drop-in
replacement for other S2 boards, build and copy the `dio_qspi` variant too:

```bash
./build.sh -s -t esp32s2 -b idf-libs dio 80m tinyusb_cdc_only
# then copy out/tools/esp32-arduino-libs/esp32s2/dio_qspi into $PKG/dio_qspi
```

Alternatively, copy `dio_qspi` from the stock package. Its `libspi_flash.a` and
`include/sdkconfig.h` differ from the trimmed build only in the TinyUSB flags
you just turned off.

### 7.4 Manifest

Create `$PKG/package.json`:

```json
{
  "name": "framework-arduinoespressif32-esp32s2-libs",
  "version": "3.3.12+trimmed.1",
  "description": "ESP-IDF 5.5.5 libraries for Arduino core 3.3.12 on ESP32-S2, trimmed to CDC-only TinyUSB",
  "keywords": ["framework", "arduino", "espressif", "esp32s2"],
  "license": "LGPL-2.1-or-later",
  "repository": {
    "type": "git",
    "url": "https://github.com/espressif/arduino-esp32"
  }
}
```

The platform requires `~3.3.12`; `3.3.12+trimmed.1` is semver-compatible and
makes it obvious in the package list that this is a local build. If a
PlatformIO version rejects build metadata, use `3.3.12` and force a reinstall
with `pio pkg install -f -e adafruit_qtpy_esp32s2`.

---

## 8. Wire it into `platformio.ini`

```ini
[env:adafruit_qtpy_esp32s2]
platform = espressif32@8.0.0
board = adafruit_qtpy_esp32s2
framework = arduino
board_build.partitions = partitions.csv
board_build.f_cpu = 80000000L
test_ignore = *
build_flags = -std=gnu++2a -Os -Wall -Wextra -Werror
build_unflags = -std=gnu++11

; Override only the precompiled S2 library package. The Arduino core sources
; stay the stock 3.3.12 package.
platform_packages =
  framework-arduinoespressif32-esp32s2-libs@symlink:///absolute/path/to/klimacontrol-core3-libs

extra_scripts =
  pre:scripts/compress_web.py
  pre:scripts/get_version.py

; Keep the existing lib_deps block from the current branch unchanged.
```

Two things this snippet does **not** fix:

1. **Flash size.** The core-3 builder supplies no `-O` flag. Without `-Os`
   the image is ~1,990 KB and exceeds the 1,856 KB app partition; `-Os` brings
   it to ~1,442 KB.
2. **TSL2591.** Adafruit TSL2591 1.4.5 fails under GCC 14 with
   `-Werror=deprecated-enum-enum-conversion`. Patch the two
   `_integration | _gain` expressions with
   `static_cast<uint8_t>(...)`, or suppress the warning for C++ only via an
   extra script. This is independent of the custom framework.

For CI, replace the `symlink://` path with a versioned archive URL:

```ini
platform_packages =
  framework-arduinoespressif32-esp32s2-libs@https://example.invalid/core3/esp32s2-libs-3.3.12-trimmed.1.zip
```

The archive must contain `package.json` and the layout from §7.

After changing the package, clear the cached build and re-install:

```bash
rm -rf .pio/build/adafruit_qtpy_esp32s2
pio pkg install -e adafruit_qtpy_esp32s2
pio run -e adafruit_qtpy_esp32s2
```

If PlatformIO still uses the stock package, check which package directory was
resolved:

```bash
pio pkg list -e adafruit_qtpy_esp32s2
```

The version should show `3.3.12+trimmed.1`, not `3.3.12`.

---

## 9. Verify the image and the heap

### 9.1 Link and size

The build must succeed without:

```text
region `dram0_0_seg' overflowed
```

Compare the ELF sections against the stock core-3 build:

```bash
SZ=$(command -v xtensa-esp-elf-size || \
     find "$HOME/.platformio/packages" \
       -path '*toolchain-xtensa-esp-elf*/bin/xtensa-esp-elf-size' | head -1)
"$SZ" -A .pio/build/adafruit_qtpy_esp32s2/firmware.elf
```

Expected: `.dram0.bss` is roughly 29 KB smaller than the stock core-3 build,
and `.flash.text` / `.iram0.text` are unchanged apart from the normal TinyUSB
class removal.

### 9.2 On-device heap measurement

This is mandatory per the `system-architecture` spec's revalidation scenario.
Flash the image and record:

- the boot log line `Diagnostics: heap=... (min=...)`;
- the OTA path's `Internal heap with TLS session up` line;
- `GET /api/status` (`heap`, `largest_free_block`).

Confirm:

- idle `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` stays above the 16 KB
  `LowHeapGuard` threshold;
- the OTA pre-flight gate (`MIN_FREE_INTERNAL = 12288`,
  `MIN_LARGEST_INTERNAL_BLOCK = 4096`) passes before an update;
- the 64 KB OTA-headroom requirement still holds.

If the idle internal heap is below the core-2 baseline and you want the old
margin back, move `RequestDiag`'s 2,496-byte ring (or another application-owned
buffer) to PSRAM. `EXT_RAM_BSS_ATTR` is **not** enabled in 3.3.12
(`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` is not set), so that buffer must
be allocated at boot with `heap_caps_malloc(MALLOC_CAP_SPIRAM)` rather than
annotated.

### 9.3 Functional checks

- USB CDC serial console enumerates and logs normally.
- `pio test -e native` still passes 722/722 (the native environment does not
  use the Espressif platform).
- OTA check/download works; the custom package affects only the build, not
  the OTA logic.
- No firmware feature references `USBMSC`, `USBHID`, `USBHost`, NCM, MIDI,
  audio, video, DFU, or vendor USB. If one is added later, the corresponding
  Kconfig switch must be turned back on and the DRAM budget re-measured.

---

## 10. Maintenance and rollback

### Keep the build reproducible

Commit these to the repository (or a companion `tools/core3/` directory):

```text
tools/core3/defconfig.tinyusb_cdc_only
tools/core3/patches/0001-disable-tinyusb-host.patch
tools/core3/build-esp32s2-libs.sh   # clone/pin/build/package wrapper
tools/core3/README.md
```

Record the exact pins in the README and in the artifact's `versions.txt`:

```text
lib-builder: 6671d0bd65cdb9d4cc1001b759e8610de945a8d5
esp-idf: v5.5.5 b774170ff46c393eeb5e495ea37936038d3f4f4f
arduino: e7c1e8cd47bcaf02da3a00d81b886d057d93c7d9
tinyusb: 88c7f0b812ad516adc670bf67627911021f0951e
```

### CI

Building the framework in every CI run is too slow. Build it once, publish it
as a versioned archive, and cache it. Keep a checksum in the repository so a
silent replacement is detectable:

```bash
shasum -a 256 esp32s2-libs-3.3.12-trimmed.1.zip
```

For local development, `symlink://` is convenient; for release builds use the
archive URL so the build is reproducible on a clean machine.

### Updating the platform

When `espressif32` / Arduino core changes:

1. Rebuild the custom libs against the new builder, Arduino, IDF, and TinyUSB
   commits.
2. Re-apply the TinyUSB defconfig and host patch; check that the Kconfig names
   still exist.
3. Re-measure the archive size and the resulting `dram0_0_seg` headroom.
4. Re-measure the on-device heap thresholds.
5. Re-run the native tests and the USB/OTA smoke tests.

### Rollback

Remove the `platform_packages` override and run:

```bash
pio pkg install -e adafruit_qtpy_esp32s2
```

PlatformIO will re-fetch the stock `framework-arduinoespressif32-esp32s2-libs`.
The stock core-3 package will still fail to link on the S2; rollback means
returning to `espressif32@7.1.3`, not to a working core-3 build.

---

## 11. Risks and trade-offs

- **Fork ownership.** This is a patched precompiled framework, not a project
  setting. It must be rebuilt and tested for every core/IDF update. The design
  document rejected this as a permanent maintenance burden; the ESP32-S3 path
  avoids it.
- **USB feature loss.** NCM, MSC, HID, MIDI, audio, video, DFU, vendor, and
  USB host disappear from the firmware. The device only uses CDC serial today;
  adding any of those features later requires re-enabling the class and
  redoing the DRAM budget.
- **Heap margin.** The expected 23.2 KB free internal DRAM is ~1.4 KB below
  the core-2 baseline. The 16 KB guard and OTA gate must be confirmed on
  hardware. If the measured idle heap is too low, move an application buffer
  to PSRAM or reduce CDC ports before lowering any threshold.
- **No signed vendor artifact.** The custom package is built from pinned
  upstream sources but is not an Espressif release. Pin commits and checksums,
  and keep the build recipe in-tree.
- **Flash is still tight.** `-Os` is required; do not rely on the core-3
  builder's default optimization. Verify the image stays under 1,856 KB.

---

## Appendix: Source references

- `openspec/changes/upgrade-espressif32-platform/design.md` — D4 and the
  measured DRAM/flash analysis.
- `espressif/esp32-arduino-lib-builder`, commit
  `6671d0bd65cdb9d4cc1001b759e8610de945a8d5`:
  - `build.sh` — `-t esp32s2 -b idf-libs`, version recording.
  - `configs/defconfig.common`, `configs/defconfig.esp32s2`,
    `configs/defconfig.qio_ram`, `configs/defconfig.qio`,
    `configs/defconfig.80m`.
  - `components/arduino_tinyusb/Kconfig.projbuild` — class switches.
  - `components/arduino_tinyusb/include/tusb_config.h` — `CFG_TUD_*` /
    `CFG_TUH_*` mapping.
  - `tools/copy-libs.sh` — package layout and `sdkconfig.h` generation.
- PlatformIO `espressif32@8.0.0` builder:
  - `builder/frameworks/arduino.py` — resolves
    `framework-arduinoespressif32-<mcu>-libs`.
  - `framework-arduinoespressif32/tools/platformio-build.py` — `TARGET_LIBS_DIR`,
    `flags/`, `qio_qspi/include`, bootloader selection.
- ESP-IDF 5.5.5 `components/freertos/heap_idf.c`:
  - `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` controls whether
    `xTaskCreateStatic` accepts PSRAM stacks (relevant only if you later move
    OTA stacks to PSRAM).
