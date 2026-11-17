# Tasks — add-display-warning-icon

## 1. Warning policy (native-testable core)

- [x] 1.1 Add `src/display/WarningPolicy.{h,cpp}`: `WarningToken` enum (`NONE`, `OVERHEAT`, `FROST`, `SENSOR`, `ACTUATOR`, `HUMID` — enum order = priority), condition input struct/bitmask, `evaluate(conditions, nowMs)` returning the current token with clear-dwell state. No Arduino/FreeRTOS includes. Verify: compiles in `pio run -e native`.
- [x] 1.2 Add threshold hysteresis inside the policy: humidity fires at ≥ threshold, releases below threshold − margin; frost fires at ≤ threshold, releases above threshold + margin; margins as compile-time constants. Verify: `pio test -e native` unit tests for boundary hover, decisive crossing, and NAN/unavailable inputs.
- [x] 1.3 Unit-test priority selection and the clear dwell: most-severe wins when multiple conditions active; onset immediate; clearance only after `WARNING_CLEAR_DWELL_S` of continuous inactivity; wrap-safe elapsed-time comparison (millis rollover test). Verify: `pio test -e native`.

## 2. Configuration

- [x] 2.1 Extend `Config::DisplayConfig` with `float warn_frost_c = NAN` and `uint8_t warn_humidity_pct = 0`; add NVS keys `warn_frost` / `warn_hum` with compile-time `nvsKeyFits` static asserts; load/save in `loadDisplayConfig()`/`saveDisplayConfig()`. Verify: build for `adafruit_qtpy_esp32s2`; native test for the validation function if one covers DisplayConfig.
- [x] 2.2 Extend `validateDisplayConfig()`: non-finite or below-minimum frost threshold → disabled (`NAN`); humidity > 100 → disabled (0). Out-of-range stored values fall back to disabled. Verify: native unit tests for validation fallback.
- [x] 2.3 Extend `DisplayRoutes.cpp`: GET returns `warn_frost_c` (null when NAN) and `warn_humidity_pct`; POST accepts both (JSON null → NAN, 0 → disabled) with CSRF and clamp-don't-reject behaviour; no panel driving on threshold-only changes. Verify: manual curl with/without CSRF header; `pio run -e adafruit_qtpy_esp32s2`.

## 3. Rendering

- [x] 3.1 Add warning-slot layout constants and the drawn icon to `EPaperDisplay.cpp`: filled triangle + white `!` bar in the left margin (x ≈ 8..55) inside the partial window, `WARNING_LABELS[]` table indexed by token, 5x7 label centered below the triangle. Verify: `pio run -e adafruit_qtpy_esp32s2`; visual check on hardware.
- [x] 3.4 Shift the icon slot 5 px down (triangle y 53..95, label y 105) and add a mirrored second icon in the right margin (x ≈ 145..192); both slots always show the same token. Update `scripts/render_display_mock.py` to match. Verify: `pio run -e adafruit_qtpy_esp32s2`; `python3 scripts/render_display_mock.py`; visual check on hardware folded into 5.2.
- [x] 3.2 Thread the warning token through `DisplayManager` (gather conditions from `SensorController::getSnapshot().valid`, `TemperatureController::isSafetyShutoffEngaged()`, computed `ControlState`, thresholds vs. snapshot values) and `EPaperDisplay::render()` (one new `WarningToken` parameter). Values and footer keep their existing geometry. Verify: `pio run -e adafruit_qtpy_esp32s2`.
- [x] 3.3 Implement onset-bypass in `DisplayManager::update()`: token change to a non-`NONE` token refreshes immediately; `NONE` and unchanged tokens go through the ordinary `RefreshPolicy` floor. Pass the token into `RefreshPolicy::evaluate()` as a change-detection input. Verify: `pio test -e native` for the policy-level change detection; hardware check for immediate onset.

## 4. Web UI

- [x] 4.1 Add frost and humidity threshold inputs to the settings page's E-Paper Display section, wired to `POST /api/display` with the CSRF header, showing `null`/`0` as disabled. Verify: `python3 scripts/compress_web.py` regenerates `src/generated/*_gz.h`; manual check in browser.

## 5. Verification

- [x] 5.1 Full test suite: `pio test -e native` and `pio run -e adafruit_qtpy_esp32s2` pass. Verify: clean output from both commands.
- [ ] 5.2 Hardware observation: enable a threshold warning, confirm the icon appears within one tick of the crossing (partial refresh, no full-panel flash), values unmoved; confirm dwell-delayed clearance; confirm OVERHEAT shows while the safety shutoff is engaged. Verify: observed on the device; no new full-refresh log lines attributable to warnings.
- [x] 5.3 Run `openspec validate --all --strict` and the repo pre-commit checks (clang-format 19.1.7, shellcheck). Verify: all pass.
