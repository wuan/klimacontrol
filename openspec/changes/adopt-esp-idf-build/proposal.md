# Proposal

## Why

The firmware is built against a precompiled Arduino core whose ESP-IDF
`sdkconfig` cannot be changed. On the pinned 2.0.17 SDK that means
`CONFIG_PM_ENABLE` is not set, so `esp_pm_configure()` cannot enable DFS or
tickless idle; WiFi/lwIP buffer counts, heap policy, brownout threshold and
watchdog configuration are likewise fixed. Those are exactly the hardware-level
controls this project needs, and `POWER_OPTIMIZATION.md` already records the
consequence: power management "does NOT work reliably in Arduino framework".

The same pinning turns every Arduino core upgrade into a cliff: `platformio.ini`
is held at 7.1.3 because core 3.x reworks WiFi events and GCC 14 tightens
format checking. Owning the build configuration removes that ceiling without
rewriting the application.

## What Changes

- Add an ESP-IDF build path through PlatformIO `framework = espidf, arduino`,
  compiling Arduino core from source under IDF instead of consuming precompiled
  static libraries.
- Add a project-owned `sdkconfig.defaults`, a root `CMakeLists.txt`, and a
  `src/CMakeLists.txt`.
- Transcribe the settings the precompiled SDK supplied implicitly into explicit
  configuration: target, 4 MB flash, 80 MHz CPU, PSRAM malloc policy, custom
  partition table, rollback enable, flash core dump, Arduino variant/autostart,
  and USB CDC console.
- Keep the current Arduino-framework environment during migration as an A/B
  fallback; release the IDF build only after hardware parity.
- After parity, enable PM/DFS/tickless and other tuning as individually
  measured experiments, each revertible.
- Defer the Arduino 3.x / IDF 5.5 step and every library replacement.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This phase changes only how the firmware is built and configured; no
observable capability behaviour changes, so no requirement delta applies. The
`system-architecture` text that names the build path is documentation to update
when the build command actually changes, not a new capability requirement.

## Impact

- **Build**: `platformio.ini` environments, `CMakeLists.txt`,
  `src/CMakeLists.txt`, `sdkconfig.defaults`; CI build and release jobs.
- **Source**: none required for the parity step. `src/main.cpp` gains optional
  power-management initialisation only in the capability phase.
- **Diagnostics**: USB CDC console, reset reason, and the flash core-dump path
  must survive the build change; they are verification items, not assumptions.
- **Tooling**: the `native` test environment is untouched. Firmware build time
  increases because Arduino core is compiled from source.
- **Out of scope**: replacing Arduino libraries (web server, sensors, display,
  MQTT), adopting Arduino core 3.x, and dropping Arduino entirely.
