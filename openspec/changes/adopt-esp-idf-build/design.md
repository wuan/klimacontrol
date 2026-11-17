# Design

## Context

See `proposal.md` for motivation. This design covers the build-platform part
only.

The current firmware is built by PlatformIO's `arduino` framework on
`espressif32@7.1.3`, which ships ESP-IDF 4.4.7 and Arduino core 2.0.17. The
Arduino core is consumed as precompiled static libraries whose `sdkconfig` is
not editable. Verified constraints that shape the approach:

- PlatformIO's ESP-IDF builder supports `framework = espidf, arduino`: it adds
  the Arduino framework directory as an IDF component and propagates its
  defines (`builder/frameworks/espidf.py`).
- For mixed Arduino+IDF projects, `platform.py` deliberately pins the IDF
  version to the one Arduino expects; on 7.1.3 that is IDF 4.4.7.
- Arduino core 2.0.17 ships `CMakeLists.txt` and `Kconfig.projbuild`, and its
  CMake version check accepts IDF `4.4.0 .. 4.4.99`.
- Arduino core 3.3.12 (platform 8.0.0) requires IDF `>=5.3,<6.2`.
- The application already calls IDF APIs directly (`esp_ota`,
  `esp_http_client`, `esp_task_wdt`, `heap_caps`, `esp_core_dump`), so the
  Arduino layer is not the only hardware interface.

Fixed constraints: single-core ESP32-S2, internal-SRAM-only budgets for task
stacks and WiFi/lwIP/mbedTLS, `native` tests must keep working, and deployed
NVS configuration must survive.

## Goals / Non-Goals

**Goals:**

- Own the build configuration: project `sdkconfig.defaults`, CMake entry
  points, and a pinned IDF line.
- Preserve runtime behaviour exactly during the parity phase.
- Make power, memory, watchdog, brownout and diagnostic settings controllable
  and revertible.
- Keep the `native` test environment and release packaging intact.

**Non-Goals:**

- Replacing Arduino libraries (web server, MQTT, sensors, display).
- Adopting Arduino core 3.x in this change.
- Changing control, sensor, HTTP or MQTT behaviour.
- Dropping Arduino entirely.

## Decisions

### D1: Use `framework = espidf, arduino` on the pinned platform for parity

The first build uses PlatformIO's mixed framework on `espressif32@7.1.3`, so
Arduino core 2.0.17 is compiled from source under IDF 4.4.7 with a project-owned
`sdkconfig`. This changes no application code, and it isolates build-system risk
from API-migration risk.

Alternatives considered:

- **Pure `framework = espidf`** — requires replacing every Arduino library now.
  That is the library-surface decision, not this one.
- **Arduino 3.3.12 / IDF 5.5 immediately** — forces the WiFi event callback
  rewrite and the `%u` format pass in the same change.
- **Rebuilding the precompiled Arduino SDK out of tree** — reproducible only by
  owning a fork of the SDK, and it does not remove the upgrade cliff.

### D2: Keep both environments during migration

Add the IDF environment alongside the current Arduino one. CI builds both;
release selects one. This gives an A/B comparison on the same commit and a
one-line fallback if the new build misbehaves on hardware.

### D3: Transcribe the implicit SDK settings explicitly

The settings the precompiled SDK supplied must become visible. The initial
`sdkconfig.defaults` is a transcription of the installed 2.0.17 S2 SDK, not a
fresh IDF default set:

| Area | Setting | Why it is explicit |
|---|---|---|
| Target | `CONFIG_IDF_TARGET="esp32s2"` | Board identity |
| Flash | 4 MB, QIO, 80 MHz | Matches the shipped module |
| CPU | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80` | Replaces `board_build.f_cpu` |
| Core | `CONFIG_FREERTOS_UNICORE=y` | S2 is single-core |
| PSRAM | `CONFIG_SPIRAM=y`, `USE_MALLOC`, `RESERVE_INTERNAL=0`, `TRY_ALLOCATE_WIFI_LWIP` | The memory spec depends on this exact policy |
| Partitions | custom `partitions.csv` | Dual-app OTA layout |
| Rollback | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` | `confirmRunningImage()` semantics |
| Core dump | enable-to-flash, ELF format | The boot summary path depends on it |
| Arduino | `CONFIG_ARDUINO_VARIANT="adafruit_qtpy_esp32s2"` | Pin map; default is generic `esp32s2` |
| Arduino | `CONFIG_AUTOSTART_ARDUINO=y` | Default is `n`; `setup()`/`loop()` otherwise never run |
| Arduino | `CONFIG_ARDUINO_RUNNING_CORE=0`, `ARDUINO_EVENT_RUNNING_CORE=0` | Single-core task placement |
| Console | `-DARDUINO_USB_CDC_ON_BOOT=1` | Otherwise `Serial` is UART0 and the USB log is silent |

The last three are silent-failure traps: the build succeeds and the device
boots, but pins, console or `setup()` are wrong. They are verification items,
not assumptions.

### D4: Enable power management only after parity, as measured experiments

`CONFIG_PM_ENABLE` and tickless idle are the capabilities this change exists to
unlock, but enabling them in the same step as the build change would conflate
two failure sources. Each capability is enabled separately, measured, and kept
only if it improves without regression. `POWER_OPTIMIZATION.md` records that
light sleep was previously blocked; the build change removes the compile-time
blocker but does not guarantee the runtime result. USB CDC and WiFi activity may
still keep the chip awake.

### D5: Re-measure, do not inherit, memory and stack numbers

The stack sizes and heap floors in the specs were measured under the current
Arduino task model. Keeping the same Arduino core makes the numbers likely to
transfer, but the parity phase records fresh HWM and heap values. Any future
pure-IDF step changes the task topology and invalidates them entirely.

### D6: Defer Arduino 3.x / IDF 5.5 / pure IDF

Moving to platform 8.0.0 pins the mixed-mode IDF to the 5.5 line and Arduino
core 3.3.12. That is a separate change with known, bounded work:

- `WiFi.onEvent` callback becomes `void (*)(arduino_event_id_t)`; the current
  two-argument lambda with a capture no longer converts.
- 125 `%u` format arguments across 25 files trip `-Werror=format` with GCC 14.
- WiFi event type names and core-3.x quirks need a compile pass.

Pure IDF (IDF 6.1) belongs to the library-surface decision.

### D7: Preserve NVS identity

Keep namespace `klima` and every existing key name and value type. Arduino
`Preferences` and raw NVS use the same store, so deployed configuration can
survive if the encoding is preserved. The parity phase round-trips a saved
configuration to prove it rather than assume it.

## Risks / Trade-offs

- **Silent Arduino config traps** (autostart, variant, USB CDC) → make each one
  an explicit verification item; compare boot log and pin behaviour against the
  Arduino build.
- **Build time and CI cost** → Arduino core compiles from source; accept it for
  the migration, measure it, and keep the precompiled path available during
  parity.
- **IDF 4.4.7 is EOL** → parity buys control, not freshness. Schedule the 5.5
  step as a separate change with its own release window.
- **PM may not deliver light sleep** → treat Phase 3 as an experiment; revert
  flags individually and record the measurement.
- **Flashing/upload path changes** → verify esptool upload with the custom
  partition CSV before relying on OTA for recovery.
- **Brownout observation collision** → `assess-display-brownout-risk` observes
  the current firmware. Finish that observation before the port, or redo it
  afterwards; do not straddle the boundary.
- **Binary size** → the partition cap is 1900 KB; check the IDF build against
  it before enabling extra components or debug features.

## Migration Plan

| Phase | Work | Gate | Rollback |
|---|---|---|---|
| 0 | Record boot log, heap, stack HWM, size, config round-trip, HA discovery, OTA, display, current draw | Baseline captured | None needed |
| 1 | Add `framework = espidf, arduino` env, `CMakeLists.txt`, `src/CMakeLists.txt`, `sdkconfig.defaults`; keep the Arduino env | Builds; under 1900 KB; native tests green | Release the Arduino env |
| 2 | Flash and run the parity checklist on hardware | Behaviour matches baseline; console, pins, NVS, OTA, display, LED verified | Reflash the Arduino build |
| 3 | Enable PM/DFS/tickless, then WiFi PS and threshold tuning, one at a time | Measured improvement with no regression | Revert the individual flag |
| 4 | Optional Arduino 3.x / IDF 5.5 step | Phase 2 checklist again | Previous release asset |

The change is releasable at the end of Phase 2. Phase 3 is where the original
motivation is realised, and Phase 4 is a separate decision.

## Open Questions

- Which IDF line is the long-term target after parity: stay on 4.4.7, move to
  5.5 with Arduino 3.x, or go pure IDF 6.1? This affects scheduling, not the
  parity plan.
- Does tickless idle actually engage with USB CDC attached and WiFi associated?
  Answer by measurement in Phase 3.
- Does the QT Py upload path remain reliable with the custom partition table
  under the IDF builder? Verify before Phase 2 sign-off.
