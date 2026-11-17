# Tasks

## 1. Source compatibility with core 3.x

- [x] 1.1 Move `src/Network.h` and `src/Network.cpp` into `src/network/` and update all nine `#include "Network.h"` sites to `"network/Network.h"`; verify no bare `#include "Network.h"` remains under `src/`, `test/` or `include/`, and that `git status` shows renames rather than delete-plus-add
- [x] 1.2 Move `class Network` and `enum class NetworkMode` into `namespace Net` and qualify the type at the remaining use sites, because core 3.x also declares a global object named `Network` (`extern NetworkManager Network;`) that hides a global class of the same name; verify every translation unit that includes `<WiFi.h>` compiles with `Net::Network*` / `Net::Network&`
- [x] 1.3 Record both collisions at the top of `src/network/Network.h`, so the file is not moved back to `src/`; verify the comment names the `Network.h` shadowing and the `extern NetworkManager Network;` variable
- [x] 1.4 Convert the `%u` consumers of `uint32_t` in `ARDUINO`-only translation units by casting the argument with `static_cast<unsigned>(...)`, driving the list from compiler diagnostics rather than a textual replace; verify all firmware translation units compile with `-Werror` and no `-Wformat` diagnostic remains
- [x] 1.5 Apply the same cast in the five translation units that also build natively — `Config.cpp`, `LowHeapGuard.cpp`, `SensorController.cpp`, `TemperatureController.cpp`, `MqttPayload.cpp` — keeping the `R"({...})"` JSON templates intact rather than un-raw-ing them; verify `pio test -e native` compiles and passes
- [x] 1.6 Audit the arguments that are not `uint32_t` (`uint8_t`/`uint16_t` promoted to `int`, plain `unsigned`, `size_t`) and confirm the compiler accepts each unchanged, so no argument was cast only to silence a warning that did not exist; verify the diff touches only genuinely mismatched arguments
- [x] 1.7 Restore `I2CDevice` as an aggregate in `src/I2CScanner.h` (C++20 no longer accepts a user-declared `= default` constructor in brace-init); verify `src/I2CScanner.cpp` compiles and `devices.push_back({addr, type})` still works
- [x] 1.8 Replace `WiFiClass::status()` with `WiFi.status()`, because `WiFiSTAClass::status()` was `static` in 2.0.17 and is an instance method in 3.3.12; verify the three affected files compile and that the still-static `mode()` / `getMode()` calls are left alone
- [x] 1.9 Verify the framework's `#include "Network.h"` resolves to the Arduino `Network` library rather than the project header, using `-H` on a unit that pulls in `WiFi.h`; verify it lists `framework-arduinoespressif32/libraries/Network/src/Network.h`

## 2. Record the blocker

- [x] 2.1 Measure the DRAM and flash difference between the pinned and attempted platforms, including the internal-heap headroom before and after; verify the numbers reconcile with the linker's reported overflow (~6.2 KB) and identify the dominant contributor (precompiled TinyUSB classes)
- [x] 2.2 Rewrite the `platformio.ini` pin comment so it names the blocker, states that core-3 source compatibility is already in place, and points the next reader at the ESP32-S3-class capacity argument; verify `platform = espressif32@7.1.3` is unchanged and no 8.0.0 reference or core-3-only build flag remains
- [x] 2.3 Record the analysis as decision D4 and in the risks section of `design.md`, including why trimming application statics and rebuilding the SDK were rejected; verify a reader can answer "why is this not bumped?" from the repo alone

## 3. Verification on the pinned platform

- [x] 3.1 Build the firmware with the pinned `7.1.3` platform and confirm the image size matches the shipped release (`1,431,008` bytes for every generated `firmware.bin`); verify no `-Wformat` or other new diagnostic appears
- [x] 3.2 Run `pio test -e native` and confirm all 722 test cases pass, including `test_mqtt_payload`, which exercises the cast-and-raw-string path; verify the run is clean after clang-format
- [x] 3.3 Run `pre-commit run --all-files` and `openspec validate upgrade-espressif32-platform --type change`; verify both are clean for the files this change touched
- [x] 3.4 Review the final diff and confirm it is source-only plus the pin comment: no platform bump, no dependency change, no partition change, and no `Net::Network` reference left unqualified where it would break the build

## Workflow follow-up

- Archive this change once 3.1–3.4 pass.
- Open a separate change for the platform bump when it can run on hardware with DRAM headroom (ESP32-S3 class); this change's source work is a prerequisite that is already satisfied.
- Open the separate change for the pin-staleness signal (scheduled job or dependency bot); it is explicitly out of scope here.
- Update the stale `src/Network.cpp` path references in `openspec/specs/system-architecture/spec.md` and `docs/` after the move, which needs MODIFIED entries for requirements this change does not otherwise touch.
