## Why

The firmware has no concept of local time. `Network::getCurrentEpoch()`
(`src/Network.h:218`) returns the NTP epoch, and `NTPClient ntpClient(wifiUdp)`
(`src/Network.cpp:64`) is constructed with no offset argument, so that epoch is
UTC. Nothing anywhere calls `localtime_r`, `tzset`, `configTime` or `strftime` —
a grep for all of them across `src/` returns nothing.

Until now that was harmless, because no consumer rendered a wall-clock time for
a human. MQTT publishes the raw epoch, which is correct for machine consumers,
and the syslog output does not emit a timestamp at all. The e-paper display
added in `add-eink-display` is the first consumer that shows a time to a person,
and it currently shows UTC: two hours behind local time in Central European
Summer Time, one hour behind in winter.

A fixed offset would not be enough. Any offset correct in January is wrong in
July across most of Europe, North America and Australasia, so the setting has to
carry daylight-saving *rules*, not just a number.

## What Changes

- Add `src/support/LocalTime.{h,cpp}` — a small, Arduino-free helper wrapping the
  POSIX timezone facilities that newlib already provides:
  - `applyTimezone(const char *tz)` — `setenv("TZ", …)` + `tzset()`
  - `isPlausibleTimezone(const char *tz)` — shape check, deliberately permissive
  - `formatLocalHhMm(char *out, size_t n, uint32_t epoch)` — `"HH:MM"` in local
    time, empty string when the epoch is 0 (NTP unsynced)
  - `formatLocalDate(char *out, size_t n, uint32_t epoch)` — `"YYYY-MM-DD"`,
    for future consumers
- Store the timezone as a **POSIX TZ string** (e.g. `CET-1CEST,M3.5.0,M10.5.0/3`)
  in a new `DeviceConfig::timezone[48]` field, defaulting to `UTC0`, under the
  NVS key `timezone` (8 characters, within the `PrefsKeys.h` limit). Add
  `ConfigManager::updateTimezone()` alongside the other partial-update methods,
  and extend `validateDeviceConfig()` to fall back to `UTC0` for an empty or
  implausible value.
- Apply the timezone once at boot in `setup()`, immediately after
  `config.begin()` and before anything can format a time.
- Switch `Display::DisplayManager::formatClock()` from its hand-rolled
  `epoch % 86400` arithmetic to `Support::formatLocalHhMm()`, so the e-paper
  footer timestamp shows local time with correct DST.
- Add `GET`/`POST /api/settings/timezone`, following the existing
  `/api/settings/elevation` shape: CSRF-protected, validated, persisted, and —
  unlike most settings routes — applied **live** via `applyTimezone()` without
  requiring a restart, because `tzset()` has no other state to reconcile.
- Add a timezone selector to the Device section of `data/settings.html`: a
  dropdown of ~20 common zones mapping display names to POSIX strings, plus a
  "Custom" free-text option for anything not listed. The zone table lives in the
  HTML, not in firmware flash, so extending it costs nothing on-device.
- Add `test/test_local_time/` covering DST transitions, the `UTC0` default, the
  unsynced sentinel, and validation — verified on the host, where BSD libc
  implements the same POSIX TZ grammar as newlib.

## Capabilities

### New Capabilities

- `local-time`: timezone storage and application, DST handling via POSIX rules,
  local-time formatting helpers, and the unsynced-clock sentinel.

### Modified Capabilities

- `configuration`: `DeviceConfig` gains `timezone`; `validateDeviceConfig()`
  gains a timezone fallback.
- `http-api`: new `GET`/`POST /api/settings/timezone`.
- `web-interface`: the Device settings section gains a timezone selector.

## Impact

- **New source files**: `src/support/LocalTime.{h,cpp}`,
  `test/test_local_time/test_local_time.cpp`
- **Modified**: `src/Config.{h,cpp}`, `src/PrefsKeys.h`,
  `src/routes/SettingsRoutes.cpp`, `src/display/DisplayManager.cpp`,
  `src/main.cpp`, `data/settings.html`, `platformio.ini` (native
  `build_src_filter`)
- **No new dependencies.** POSIX `setenv`/`tzset`/`localtime_r` are already in
  newlib on this toolchain and in the host libc for the native tests.
- **No flash cost for the zone table** — it is frontend data.
- **Backward compatible**: an existing device with no `timezone` key in NVS loads
  `UTC0`, i.e. exactly today's behaviour, until an operator chooses a zone.
- **Out of scope**: an IANA tzdata database on the device; automatic geolocation
  or timezone lookup over the network; per-consumer timezone overrides;
  timestamping syslog output; changing the MQTT payload, which correctly
  publishes UTC epoch for machine consumers; a 12-hour clock format.
