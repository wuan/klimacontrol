## 1. Local time helper (native-testable, no hardware)

- [x] 1.1 Create `src/support/LocalTime.h` in `namespace Support` declaring `void applyTimezone(const char *tz)`, `bool isPlausibleTimezone(const char *tz)`, `size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch)`, `size_t formatLocalDate(char *out, size_t n, uint32_t epoch)`, plus `constexpr const char *DEFAULT_TIMEZONE = "UTC0"` and `constexpr size_t MAX_TIMEZONE_LEN = 47`. No Arduino or FreeRTOS includes
- [x] 1.2 Create `src/support/LocalTime.cpp` implementing `applyTimezone()` as `setenv("TZ", tz, 1); tzset();`, guarding against a null or empty argument by substituting `DEFAULT_TIMEZONE`
- [x] 1.3 Implement `isPlausibleTimezone()`: reject null, empty, longer than `MAX_TIMEZONE_LEN`, or containing any character outside printable ASCII. Deliberately permissive beyond that — the POSIX grammar admits quoted designations (`<+04>-4`) and fractional offsets (`IST-5:30`), and a strict parser would reject valid input
- [x] 1.4 Implement both formatters via `localtime_r()`, returning an empty string and 0 when `epoch == 0` (the established NTP-unsynced sentinel), and when `out` is null or `n` is 0
- [x] 1.5 Add `+<support/LocalTime.cpp>` and `+<test/test_local_time/>` to the `env:native` `build_src_filter` in `platformio.ini`
- [x] 1.6 Create `test/test_local_time/test_local_time.cpp` (Unity) covering: `UTC0` passthrough; CET winter (2026-01-15T12:00:00Z → 13:00) and CEST summer (2026-07-15T12:00:00Z → 14:00); the exact DST transition (2026-03-29T00:59:00Z → 01:59, T01:00:00Z → 03:00); a southern-hemisphere zone inverting the seasons; US rules differing from EU rules at the same instant; fractional offset `IST-5:30`; quoted designation `<+04>-4`; `epoch == 0` producing an empty string; null/zero-length buffers; and `isPlausibleTimezone()` accept/reject cases
- [x] 1.7 Run `pio test -e native -f test_local_time` and confirm it passes before touching any other file

## 2. Configuration

- [x] 2.1 In `src/PrefsKeys.h`, add `constexpr const char* TIMEZONE = "timezone";` to the device configuration block (8 characters, within the documented NVS limit)
- [x] 2.2 In `src/Config.h`, add `char timezone[48] = "UTC0";` to `DeviceConfig`, with a comment noting it is a POSIX TZ string carrying the DST rules and pointing at `Support::LocalTime`
- [x] 2.3 In `src/Config.h`, declare `void updateTimezone(const char *timezone);` alongside the other partial-update methods
- [x] 2.4 In `src/Config.cpp` `validateDeviceConfig()`, replace an empty or `!Support::isPlausibleTimezone()` value with `Support::DEFAULT_TIMEZONE`
- [x] 2.5 In `src/Config.cpp`, load `timezone` in `loadDeviceConfig()`, persist it in `saveDeviceConfig()`, and implement `updateTimezone()` following the shape of `updateElevation()`
- [x] 2.6 Extend `test/test_config/` with timezone cases: the struct default is `UTC0`; validation resets empty and non-printable values; validation preserves `CET-1CEST,M3.5.0,M10.5.0/3`

## 3. Apply at boot and in the display

- [x] 3.1 In `src/main.cpp` `setup()`, call `Support::applyTimezone(deviceConfig.timezone)` immediately after the device config is loaded and before `setupDisplay()`, so nothing can format a time in the wrong zone
- [x] 3.2 Log the applied timezone and the resulting local time at `ESP_LOGI` so the console shows which zone is in force
- [x] 3.3 In `src/display/DisplayManager.cpp`, replace the hand-rolled `epoch % 86400` arithmetic in `formatClock()` with `Support::formatLocalHhMm()`, keeping the existing behaviour of leaving the field blank when NTP has not synced

## 4. HTTP route

- [x] 4.1 In `src/routes/SettingsRoutes.cpp`, add `GET /api/settings/timezone` returning `{"timezone": …, "local_time": …, "synced": …}`, with `local_time` empty and `synced` false before NTP sync
- [x] 4.2 Add `POST /api/settings/timezone`: `verifyCsrfHeader()` first, reject an implausible value with 400 and an error body, otherwise `config.updateTimezone()` then `Support::applyTimezone()`
- [x] 4.3 Deliberately do **not** call `config.requestRestart()` — `tzset()` fully applies the change and no component holds derived state. Add a comment recording this so the inconsistency with the neighbouring routes is not later "fixed"

## 5. Web UI

- [x] 5.1 Add a timezone selector to the Device section of `data/settings.html`: a `<select>` of ~20 common zones with POSIX strings as values, plus a `Custom` option. Exclude zones needing the hour-24+ POSIX extension (`Asia/Jerusalem`) or with recently unstable legislation (`Africa/Cairo`) — Custom covers those
- [x] 5.2 Reveal a free-text input when `Custom` is selected; on load, match the stored value against the list and fall back to selecting `Custom` with the field populated
- [x] 5.3 Show the current local time from `GET /api/settings/timezone`, displaying a "waiting for NTP" state rather than a placeholder time when `synced` is false
- [x] 5.4 Save via `POST` with the `X-Requested-With: KlimaControl` header; on success refresh the displayed local time and do **not** claim the device is restarting
- [x] 5.5 Confirm `scripts/compress_web.py` regenerates `src/generated/settings_gz.h` during the build

## 6. Build, test, verify

- [x] 6.1 `pio run -e adafruit_qtpy_esp32s2` — SUCCESS, zero new warnings. Flash 1,310,602 B (69.0%), **+7,264 B** over the pre-timezone baseline of 1,303,338 B
- [x] 6.2 `pio test -e native` — 307/307 PASS across 22 suites (282 existing + 21 new in `test_local_time` + 4 new timezone cases in `test_config`)
- [x] 6.3 Confirmed on hardware by the operator: setting `Europe/Berlin` makes `GET /api/settings/timezone` report the expected local time, and no restart occurs — the live `tzset()` apply (D2) works as designed
- [x] 6.4 Confirmed on hardware by the operator: with the display enabled, the footer clock shows local time rather than UTC
- [x] 6.5 Confirmed on hardware by the operator: the MQTT payload's `time` field is still the UTC epoch — the timezone affects presentation only
- [x] 6.6 Archive the change with `/opsx:archive`. **Archive `add-eink-display` first**: it renames the `web-interface` requirement "Tabbed settings modal" to "Settings page sections", which this change's delta then modifies
