# Design

## Context

See `proposal.md` — Why. The facts below were measured against the two platform
packages installed on this machine, not taken from the existing pin comment.

```
                     7.1.3 (pinned)                 8.0.0 (attempted)
  Arduino core       2.0.17                         3.3.12
  ESP-IDF            4.4.7                          5.5.5
  lwIP               2.1.2                          2.2.0
  mbedTLS            2.28.7                         3.6.6
  compiler           toolchain-xtensa-esp32s2       toolchain-xtensa-esp-elf
                     GCC 8.4                         GCC 14.2
  uint32_t           unsigned int                   unsigned long
```

The compiler switch is the reason the pin comment names `-Wformat`, and it is
a type-identity change, not a strictness change: `-Wformat` compares types, and
`%u` names `unsigned int` while `uint32_t` is now `unsigned long`. A probe
confirms `Show<uint32_t>` resolves to `Show<unsigned int>` on GCC 8.4 and
`Show<long unsigned int>` on GCC 14.2, and that `%u` against a `uint32_t` is
`-Werror=format` on the latter.

Verified as unchanged between the cores (so not part of this change):

- `CONFIG_SPIRAM_USE_MALLOC=y` and `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`,
  so the internal-SRAM accounting model still holds.
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, so `confirmRunningImage()` keeps
  its meaning.
- `MBEDTLS_PLATFORM_STD_CALLOC` still maps to `esp_mbedtls_mem_calloc`, so the
  PSRAM-offload hook in `src/ota/http/TlsAllocator.cpp` stays on the
  allocation path.
- Installed `AsyncTCP` 3.5.0 and `ESPAsyncWebServer` 3.12.1 already carry
  core-3 branches.

Measured budget on the target board:

```
                         core 2.0.17    core 3.3.12     delta
  .dram0.data                17,924        18,226
  .dram0.bss                 64,112        87,400      +23,288
  .dram0_reserved_for_iram   65,364        72,680       +7,316
  ----------------------------------------------------------------
  static total               82,036       105,626      +30,932
  available for static                            99,352
  internal heap headroom     24,608             0      (over by 6,184)
```

## Goals / Non-Goals

**Goals**

- Land every source-level change core 3.x requires, with the firmware still
  building and linking on the pinned platform.
- Make the blocker discoverable from the repo, not from a chat log.

**Non-Goals**

- Bumping the platform. Doing so would ship an image that cannot link; the
  analysis is recorded so the bump can be picked up on hardware that can hold
  it.
- Restoring the RTC hardware watchdog for IDF 5. On the pinned IDF 4.4 build it
  is already active via `soc/rtc_wdt.h`; only the IDF 5 branch is a no-op, and
  that branch is unreachable until the bump happens.
- Remeasuring heap thresholds or task stack high-water marks. Neither moves on
  the pinned platform; the spec now carries the obligation for when they do.
- Bumping libraries for core 3.x. The only library failure seen (Adafruit
  TSL2591 1.4.5 and C++20's deprecated enum-to-enum bitwise OR) is unreachable
  on the pinned platform.
- Replacing `build_type = debug` with an explicit optimisation level. The
  core-3 builder drops the `-O` flag that the 7.x builder supplies, which
  inflates flash by ~550 KB; that is recorded for the future bump, not changed
  here.

## Decisions

### D1. Land the source work now; keep the pin

Alternatives: bump anyway and shrink something else until it links; revert and
throw the work away; switch to a rebuilt SDK.

The source changes are verified platform-neutral: building the current working
tree against `espressif32@7.1.3` succeeds and produces a 1,431,008-byte image,
identical in size to the shipped firmware. So they cost nothing to keep and
remove all the mechanical migration work from a future bump. Shrinking until it
links is not viable (D4). Switching to a rebuilt SDK is a permanent maintenance
burden for a hygiene upgrade (D4).

### D2. Cast the argument to match the format; keep `%u` and `%lu`

Alternatives: blanket `%u` -> `%lu` (rejected: on the `native` env `uint32_t`
is `unsigned int` and `unsigned long` is 64-bit, so it breaks `pio test -e
native` and the five shared translation units — `Config.cpp`,
`LowHeapGuard.cpp`, `SensorController.cpp`, `TemperatureController.cpp`,
`MqttPayload.cpp` — that build on both targets); `PRIu32` from `<cinttypes>`
(portable, but cannot be concatenated into a raw string literal, and two of the
affected sites are JSON templates written as `R"({"time":%u,...})"`).

A `uint32_t` argument becomes `static_cast<unsigned>(x)` and keeps `%u`:

```cpp
ESP_LOGW(TAG, "Low internal heap %u bytes (%u/%u consecutive)",
         static_cast<unsigned>(freeInternalBytes), lowStreak, RESTART_STREAK);
```

`uint32_t` always fits `unsigned` on both targets — 32-bit on xtensa, and
`unsigned` is 32-bit on the 64-bit host. The codebase already solves this with
casts in a dozen places (`static_cast<unsigned long>(now / 1000)` for `%lu` at
`Network.cpp`, `static_cast<unsigned>(mode)` in `WebServerManager.cpp`), so the
new code reads like the old code.

Values that are not `uint32_t` — `uint8_t`/`uint16_t` promoted to `int`, plain
`unsigned`, `size_t` — need no change: GCC does not warn on the
`int`-versus-`unsigned int` distinction under `%u`. That was measured, so the
diff touches only genuinely mismatched arguments.

### D3. Move `Network` into `src/network/` and into `namespace Net`

Core 3.x ships an Arduino **`Network` library** with a header named
`Network.h` and a global object named `Network`:

```c
// libraries/Network/src/WiFiGeneric.h:44
#include "Network.h"            // quoted: own dir, then -I order

// libraries/Network/src/NetworkManager.h:37
extern NetworkManager Network;  // a global *variable* named Network
```

While this project's file sat at `src/Network.h`, `-I src` preceded the
framework path, so `WiFiGeneric.h` resolved that include to this project's
header; `NetworkEventCb`, `network_event_handle_t` and `arduino_event_id_t`
were never declared, and every TU touching WiFi failed in a cascade. Under
`src/network/` the bare name cannot resolve there, because `src` is the only
project include root.

That fixed the header half; the variable half cannot be fixed by a path. A
global `class Network` is hidden by `extern NetworkManager Network;` in every
TU that includes `<WiFi.h>`, which shows up as the unmistakable
`sizeof(Network)` compiles / `Network* p;` reports "'Network' does not name a
type" pair. The class therefore becomes `Net::Network`, matching the `Net::`
classes it orchestrates (`WifiStation`, `MqttClient`, `ApProvisioning`,
`MqttPublisher`). No `using namespace Net;` exists in the tree, so no
unqualified ambiguity is introduced.

Both halves are documented at the top of `src/network/Network.h` so a future
tidy-up does not move the file back.

### D4. The platform bump is blocked by DRAM, not by code

Alternatives considered for making 8.0.0 link:

- **Trim application statics.** The only large movable buffers are the two OTA
  task stacks (19,456 B) and `RequestDiag` (2,510 B). Their stack sizes are
  measurement-locked: `UPDATE_TASK_STACK` 12,288 B against a measured ~9.3 KB
  peak, `CHECK_TASK_STACK` 7,168 B against ~5.5 KB. Moving both to PSRAM fits
  the link but leaves ~15 KB of internal heap — below the 16 KB low-heap guard,
  so the device would restart-loop.
- **Rebuild the SDK without USB classes.** `ncm_epbuf` (6,416), `mscd_epbuf`
  and `transfer_buf` (4,096 each), the endpoint buffers (3,104 × 2) and
  `hidh_epbuf` (2,688) are compiled into the precompiled
  `libarduino_tinyusb.a` from `CONFIG_TINYUSB_NCM_ENABLED=y`, `MSC_ENABLED`,
  and the HID host config. Platform 8.0.0's Arduino builder has no sdkconfig
  override hook, so this means switching platform source or maintaining a
  patched framework — a permanent burden for a hygiene change.
- **Shrink flash instead.** The flash side is already solved by optimisation
  (see Risks); it is not the constraint.

The board's pre-upgrade internal-heap headroom was 24,608 B. Core 3.3.12
consumes 30,932 B more. This is a hardware-capacity question: the ESP32-S2 has
192 KB of DRAM, and the unused USB class set alone accounts for most of the
gap. The pin stays, and this analysis is recorded in `platformio.ini` and here
so the bump is a decision, not a surprise.

## Risks / Trade-offs

- **Moving source now could regress the shipped firmware** -> verified by
  building the working tree against `espressif32@7.1.3`: success, 1,431,008-byte
  image, the same size as the shipped one, and `pio test -e native` passes
  722/722.
- **`Net::Network` is a broad rename** -> it is mechanical and compiler-checked
  in both environments; the class is only referenced by its own file and about
  ten use sites.
- **Stale path references** -> ~25 `src/Network.cpp` references in
  `system-architecture` and `docs/` now point at a moved file. Fixing those
  needs MODIFIED entries for requirements this change otherwise does not touch,
  so it is recorded as follow-up.
- **The blocker can be forgotten** -> it is written into the pin comment, the
  proposal, and D4, and the spec now requires heap-threshold revalidation after
  a core-generation change.
- **Flash headroom for the future bump** -> the core-3 builder emitted no `-O`
  flag at all (`-O0`), giving a ~1,990 KB image that exceeds the 1,856 KB
  partition. With `-Os` it is 1,442 KB. That must be made explicit when the
  bump is picked up; it is a builder regression, not a code-size regression.

## Migration Plan

1. Land the source-compatibility changes on the migration branch.
2. Rebuild on the pinned platform and confirm the image size matches the
   shipped firmware.
3. Run the native suite and the pre-commit gates.
4. Merge. Rollback is `git revert`; no firmware is deployed by this change.
5. The platform bump stays open as a follow-up on hardware with headroom
   (ESP32-S3 class), at which point the recorded source work reduces it to a
   version edit plus bench validation.

## Open Questions

- Which board carries the next generation. The ESP32-S2 cannot hold core 3.3.12
  with the current USB-class set; an S3-class part (320 KB DRAM) is the
  natural target.
- Whether `docs/POWER_OPTIMIZATION.md`'s "light sleep cannot work in the
  Arduino framework" conclusion still holds on IDF 5.5. It was reached on
  4.4.7 and remains unverified. Answering it belongs to the bump, not here.
