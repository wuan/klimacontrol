# Tasks

## 1. Display configuration model

- [x] 1.1 Add a stable `DisplayField` enum with explicit values, token mapping helpers, and `DisplayConfig.second_field` / `third_field` defaults (`Humidity`, `None`) in `src/Config.h`; verify the native config tests compile against the new fields.
- [x] 1.2 Add `disp_fld2` and `disp_fld3` to `src/PrefsKeys.h` with `Config::nvsKeyFits` static asserts; verify the native build compiles.
- [x] 1.3 Load and save both fields in `src/Config.cpp` and extend `validateDisplayConfig()` so unknown/corrupt second values fall back to humidity and unknown/corrupt third values fall back to none; add round-trip, default and fallback cases in `test/test_config`, then verify with `pio test -e native -f test_config`.
- [x] 1.4 Add token parsing/mapping tests for all eight fields plus `none`, including rejection of `none` as a second field; verify with the same native test target.

## 2. Display field metadata and formatting

- [x] 2.1 Add `src/display/DisplayField.{h,cpp}` with the field-to-measurement mapping, float/int32 variant extraction, label/unit formatting, placeholder formatting, degree-ring flag and field hysteresis; verify the native build includes the new translation unit.
- [x] 2.2 Add `test/test_display_fields/` covering humidity, dew point, pressure, sea-level pressure, CO₂, illuminance, VOC index, PM2.5, placeholders and variant extraction; add the source and test directory to the native build filters, then verify with `pio test -e native -f test_display_fields`.
- [x] 2.3 Confirm the widest rendered line (`PM2.5 8 ug/m3`) and the longest warning label (`OVERHEAT`) fit the panel at the chosen fonts; record the measurements in the test or a short display-field comment.

## 3. Refresh policy slots

- [x] 3.1 Add a `ValueSlot` type and per-slot availability/value state to `src/display/RefreshPolicy.{h,cpp}`, and update `evaluate()`, `commit()` and `reset()` to compare the temperature, second and third slots independently; verify the native policy test target compiles.
- [x] 3.2 Update the existing refresh-policy tests to the slot-based signature and add cases for pressure-only sensors, VOC-index jitter/decisive change, slot availability transitions and the minimum-interval floor; verify with `pio test -e native -f test_display_refresh_policy`.
- [x] 3.3 Re-run the warning-onset, clock, ghosting and rollover cases to confirm the existing scheduling rules are unchanged; verify the same native test target passes.

## 4. Panel layout and warning geometry

- [x] 4.1 Update `EPaperDisplay` layout constants for the two-line (`81 / 124`) and three-line (`73 / 107 / 135`) layouts, add the optional second/third value lines and the 12 pt degree-ring helper for dew point, and verify the host renderer compiles.
- [x] 4.2 Implement the 34 px temperature-anchored warning geometry for both layouts (`apex = temp - 34`, `base = temp`, label top `temp + 6`, centres `31 / 169`) and verify renderings with a long second line and an `OVERHEAT` label.
- [x] 4.3 Update `scripts/render_host/main.cpp` samples for humidity, dew point, pressure, sea-level pressure, VOC index, the two/three-line layouts and active warnings; regenerate the PNGs and visually verify no warning/value overlap.
- [x] 4.4 Update the README display mock examples and images for the new layouts; verify the documented renderer command reproduces the images.

## 5. DisplayManager integration

- [x] 5.1 Read the configured fields in `DisplayManager::begin()`, capture one sensor snapshot per update, format the second/third lines through `DisplayField`, and pass the slot readings and formatted lines to `RefreshPolicy` and `EPaperDisplay`; verify `pio run -e adafruit_qtpy_esp32s2`.
- [x] 5.2 Confirm that an unavailable configured field keeps its layout position and renders its placeholder, and that a pressure-only snapshot still repaints the pressure slot; verify through the host renderer samples and the native policy/formatting tests.
- [x] 5.3 Confirm the header, footer, splash, AP-info screen and disable/clear paths are unchanged by the new render signature; verify the Arduino build and existing display tests.

## 6. HTTP API

- [x] 6.1 Extend `GET /api/display` with `second_field` and `third_field` token strings; verify the default response is `humidity` / `none` and stored selections round-trip.
- [x] 6.2 Extend `POST /api/display` to accept both tokens, preserve an absent field, map `third_field: null` to `none`, and fall back safely for unknown tokens; verify with a CSRF-protected request and a persisted read-back.
- [x] 6.3 Keep the disable-blank and restart path unchanged and update the display-config log line; verify `pio run -e adafruit_qtpy_esp32s2`.

## 7. Settings UI

- [x] 7.1 Add the Second value and Third value selects, their option tokens, the sea-level-pressure/elevation hint and the missing-sensor hint to `data/settings.html`; load values from `GET /api/display` and submit both tokens with the CSRF header; verify the page JavaScript parses and the save payload matches the API contract.
- [x] 7.2 Regenerate `src/generated/settings_gz.h` through the existing compression hook (or `python3 scripts/compress_web.py`) and verify the updated settings page is served by the firmware build.

## 8. Integration verification

- [x] 8.1 Run `pio test -e native` and verify all native suites pass.
- [x] 8.2 Run `pio run -e adafruit_qtpy_esp32s2` and `pio run -e adafruit_qtpy_esp32s2_idf` and verify both firmware builds succeed.
- [x] 8.3 Run `openspec validate add-configurable-display-fields --strict` and the pre-commit/clang-format checks; verify this change and its source formatting are clean. If running `scripts/validate-openspec.sh`, record any pre-existing main-spec warnings separately from this change.
- [x] 8.4 On hardware with a display and the relevant sensors, verify the default two-line display, a dew-point second field, a pressure/sea-level third field and a VOC-index selection across restart; confirm placeholders appear when a selected sensor is absent.

## Workflow follow-up

- Archive the change after implementation and review are complete.
- Verify the archived specs and generated artifacts.
