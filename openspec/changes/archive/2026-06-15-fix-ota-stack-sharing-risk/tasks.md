## 1. Header: split the static buffers and drop the parking state

- [x] 1.1 In `src/OTAUpdater.h`, replace the single `otaTaskTCB` / `otaTaskStack` pair with two independent pairs: `otaCheckTCB` + `otaCheckStack[CHECK_TASK_STACK]` (7 KB) and `otaUpdateTCB` + `otaUpdateStack[UPDATE_TASK_STACK]` (12 KB)
- [x] 1.2 In `src/OTAUpdater.h`, remove the `otaTaskHandle`, `taskParked`, and `reapParkedTask()` declarations from the `#ifdef ARDUINO` block
- [x] 1.3 In `src/OTAUpdater.h`, rewrite the comment block above the new buffer pairs to describe the independent-stack model (each task owns its own buffer, self-deletes on exit) and drop the parking/reaping rationale

## 2. Implementation: simplify the task bodies and start paths

- [x] 2.1 In `src/OTAUpdater.cpp`, change `OTAUpdater::otaCheckTask` to end with `vTaskDelete(nullptr)` instead of the `taskParked.store(true); vTaskSuspend(nullptr);` pair; update the trailing comment
- [x] 2.2 In `src/OTAUpdater.cpp`, change `OTAUpdater::otaWorkerTask` to end with `vTaskDelete(nullptr)` instead of the `taskParked.store(true); vTaskSuspend(nullptr);` pair; update the trailing comment
- [x] 2.3 In `src/OTAUpdater.cpp`, change `startBackgroundCheck` to (a) drop the `reapParkedTask()` call, (b) declare a local `TaskHandle_t` for the new task, (c) pass `otaCheckStack` + `&otaCheckTCB` to `xTaskCreateStatic`, and (d) keep the `xTaskCreateStatic` failure path that rolls `checkState` back to `Failed`
- [x] 2.4 In `src/OTAUpdater.cpp`, change `startBackgroundUpdate` to (a) drop the `reapParkedTask()` call, (b) declare a local `TaskHandle_t` for the new task, and (c) pass `otaUpdateStack` + `&otaUpdateTCB` to `xTaskCreateStatic`
- [x] 2.5 In `src/OTAUpdater.cpp`, delete the entire `reapParkedTask()` function definition
- [x] 2.6 In `src/OTAUpdater.cpp`, verify that no other translation unit still references `otaTaskHandle`, `otaTaskStack`, `otaTaskTCB`, `taskParked`, or `reapParkedTask` (a project-wide `grep` is enough)

## 3. Build and test

- [x] 3.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the firmware compiles cleanly
- [x] 3.2 Run `pio test -e native` and confirm the unit tests still pass (the change is `#ifdef ARDUINO`-only, so this is a smoke test against accidental non-`ARDUINO` breakage)
- [x] 3.3 Inspect the build output's symbol table to confirm the new symbols (`otaCheckStack`, `otaCheckTCB`, `otaUpdateStack`, `otaUpdateTCB`) are present and that the old ones (`otaTaskStack`, `otaTaskTCB`, `otaTaskHandle`, `taskParked`, `reapParkedTask`) are absent

## 4. On-device validation

- [x] 4.1 Flash the new firmware to the S2 with `pio run -e adafruit_qtpy_esp32s2 -t upload` and confirm the device boots normally
- [x] 4.2 Trigger a background OTA check (`POST /api/ota/check` or the settings UI), confirm it completes and reports the latest release
- [x] 4.3 Trigger a background OTA update (`POST /api/ota/update` or the settings UI), confirm the device flashes, reboots into the new partition, and exposes the expected version
- [x] 4.4 `POST /api/ota/confirm` to mark the new partition as valid, then reboot the device and confirm it stays on the new partition (no rollback)
- [x] 4.5 Repeat the check-then-update cycle a second time in the same boot session to exercise the "second task start on the same buffers" path that the old design relied on `reapParkedTask()` for — with the new code this should "just work" because each task owns its own buffer

## 5. Stack-size tuning (post-validation follow-up)

- [x] 5.1 Bump the high-water-mark log lines in `src/OTAUpdater.cpp` (otaCheckTask, otaWorkerTask) from `ESP_LOGD` to `ESP_LOGI` so they appear in the default serial output
- [x] 5.2 Re-build and flash the firmware; trigger a check and an update; capture the two high-water-mark values from the serial output
- [x] 5.3 Re-tune `CHECK_TASK_STACK` and `UPDATE_TASK_STACK` in `src/OTAUpdater.h` based on the captured values (target: actual usage + 25% safety margin, round up to the next 1 KB); re-build and confirm the high-water mark still shows >0 bytes free at the new size
- [x] 5.4 Re-flash the firmware with the tuned 7 KB / 12 KB sizes; trigger a check and an update; confirm both high-water marks are still > 0 bytes free (expected: check ≥ 1024, update ≥ 2048 bytes)
