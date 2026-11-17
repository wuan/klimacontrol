# Tasks

## 1. Capture the parity baseline

- [x] 1.1 Record the current boot baseline on the shipped Arduino build — boot log, `Reset reason:`, internal heap free and largest block, `largest_free_block` from `/api/status`, network and sensor stack HWM lines — and verify the table is complete enough that Phase 2 can be compared against it line by line
- [x] 1.2 Record the current firmware size and partition usage from the build output, and verify the recorded size is below the 1900 KB cap
- [x] 1.3 Save a configuration snapshot (settings page values, sensor assignments, control tuning, MQTT/display settings), reboot, and verify every value returns unchanged
- [x] 1.4 Confirm the deployed NVS layout: namespace `klima`, key names, and value types — verify the list covers every key written by `ConfigManager`
- [x] 1.5 Record current idle and transmit current, plus display refresh cadence, and verify the figures are dated and attributable to a specific build
- [x] 1.6 Finish or consciously defer `assess-display-brownout-risk` before the port starts, and verify the decision is recorded so the observation is not straddling the build change

## 2. Add the ESP-IDF build path

- [x] 2.1 Add the IDF environment in `platformio.ini` (`framework = espidf, arduino`, platform 7.1.3, custom partitions) alongside the existing Arduino environment, and verify `pio run -e <idf-env> -t clean` configures without touching the Arduino environment
- [x] 2.2 Add the root `CMakeLists.txt` and `src/CMakeLists.txt` component registration, and verify CMake configure succeeds from a clean build directory
- [x] 2.3 Add `sdkconfig.defaults` with the transcribed target, flash, CPU, PSRAM, partition, rollback, core-dump and Arduino settings, and verify the generated `sdkconfig.<env>` contains the expected values (`CONFIG_ARDUINO_VARIANT`, `CONFIG_AUTOSTART_ARDUINO`, `CONFIG_SPIRAM_USE_MALLOC`, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`)
- [x] 2.4 Add the vendored `components/arduino_tinyusb` component (CDC-only TinyUSB), the USB CDC console build flag and board defines, and verify `Serial` output reaches a host over USB after flashing
- [x] 2.5 Add the superproject build of Arduino core from source and verify the firmware links and produces `firmware.bin` and `firmware.elf`
- [x] 2.6 Verify the IDF build stays under the 1900 KB partition cap and that `pio test -e native` still passes unchanged
- [x] 2.7 Document the dual-environment build and test commands in `AGENTS.md`, `README.md`, and the OpenSpec project context, and verify each documented command runs as written

## 3. Prove hardware parity

- [x] 3.1 Flash the IDF build and verify the boot log is readable over USB, including the `Reset reason:` line, and that the version string is the expected one
- [x] 3.2 Verify WiFi association, mDNS hostname resolution, and NTP epoch plausibility match the baseline
- [x] 3.3 Verify the saved configuration survives the build change and round-trips through a reboot unchanged
- [x] 3.4 Verify every configured sensor reads, the control loop updates, and the heating actuator drives its channel as before
- [x] 3.5 Verify the HTTP API and web UI respond, including a state-changing request with the CSRF header and one without
- [x] 3.6 Verify MQTT publishing and Home Assistant discovery reproduce the baseline entities and state topics
- [x] 3.7 Verify OTA check, update, and rollback-confirm behaviour on a test device, including the release asset name requirement
- [x] 3.8 Verify the e-paper display refreshes and the status LED follows its gradient, transmit flash, and dark-mode behaviour
- [x] 3.9 Re-measure heap free/largest block and both task stack HWMs, compare against Phase 1, and verify any changed figures are reflected in the source comments and spec evidence
- [x] 3.10 Verify the flash core-dump summary still appears on the boot after a deliberately induced panic on a test device

## 4. Enable the capabilities that motivated the port

- [ ] 4.1 Enable `CONFIG_PM_ENABLE` and call `esp_pm_configure()` in `setup()`, then verify the device still boots, associates, and serves HTTP, and record idle current against the baseline
  - Progress 2026-11-17: enabled and flashed with `max_freq_mhz=80`, `min_freq_mhz=40`, `light_sleep_enable=false`; boots and serves HTTP/MQTT, and `Network::logDiagnostics()` records PM mode counters (54% at 40 MHz over 213 s). **Idle current is still unmeasured** — no meter is connected to the capture host — so this task stays open until a reading exists. Evidence: `docs/BUILD_PARITY_BASELINE.md` §Phase 4.1.
- [x] 4.2 Enable tickless idle, measure whether light sleep actually engages with USB CDC attached and WiFi associated, and verify the result is recorded as kept or reverted
  - Result 2026-11-17: tickless idle + `light_sleep_enable=true` stopped USB CDC output while HTTP/MQTT stayed alive, and the next boot reported a Network-task panic (`heap_caps_malloc_prefer`). **Reverted**; tickless idle is off. Evidence: `docs/BUILD_PARITY_BASELINE.md` §Phase 4.2.
- [ ] 4.3 Run the WiFi power-save experiment for its declared observation window, verify stability over that window, and record keep/revert with evidence
  - Not started. The test unit already stores `wifi_sleep_mode=2` (`MAX_MODEM`); no observation window has elapsed.
- [ ] 4.4 Make and record the brownout-threshold decision in coordination with the display brownout work, and verify the `Reset reason:` behaviour remains diagnosable
  - Still deferred with `assess-display-brownout-risk`; no threshold change was made.
- [x] 4.5 Tune WiFi/lwIP buffer and heap settings if the measurements justify it, and verify `/api/status` heap fields and the low-heap guard still behave correctly
- [x] 4.6 Update `POWER_OPTIMIZATION.md` with the measured outcomes, replacing the Arduino-framework limitation statements with what the IDF build actually achieved, and verify every claim names its measurement
  - Done 2026-11-17: the guide separates Arduino from IDF, records DFS as kept and light sleep as reverted with the mode-counter evidence, and labels every mA figure as an estimate rather than a measurement. WiFi power-save and brownout outcomes remain tracked by tasks 4.3/4.4.

## 5. Integration and release

- [x] 5.1 Build both environments in CI, verify the workflow is green, and verify the native test job is unchanged
- [ ] 5.2 Point the release workflow at the chosen firmware environment and verify the published asset is still named `firmware.bin` and the size is reported
- [ ] 5.3 Run the full regression on a release candidate — native tests, `openspec validate --all --strict`, OTA update from the previous release — and verify all three pass

## Workflow follow-up

- Deferred: adopt Arduino core 3.x / IDF 5.5 as a separate change, including the `WiFi.onEvent` callback rewrite and the `%u` format pass.
- Deferred: decide whether to drop Arduino entirely (pure `framework = espidf`, IDF 6.1) as the library-surface change.
- Archive this change after the parity release ships and the capability experiments have recorded outcomes.
