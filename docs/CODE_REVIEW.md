# Klima-Control — Embedded Firmware Code Review

**Scope.** Static review of the Klima-Control ESP32-S2 firmware (Adafruit QT Py
ESP32-S2), covering `src/`, `test/`, `openspec/`, `data/`, and tooling.
Investigations ran as five parallel passes covering memory & threading, security &
network, control correctness, code quality, and display & UI. Highest-severity
findings were independently verified by reading the relevant source.

**Overall.** The codebase is unusually well-disciplined for an embedded project —
ownership is consistently via `std::unique_ptr`, the OTA path enforces HTTPS +
cert-bundle + exact asset-name + host-allowlist + strict version compare, the
parked OTA task design is correct, and the test suite is non-trivial. None of
the findings below indicate systemic rot; they are localized and individually
small.

**Severity legend.** Critical / High / Medium / Low. Critical items should be
fixed before the next release; High items should be triaged within the current
cycle.

---

## CRITICAL — fix first

### 1. ~~Syslog-enabled flag is silently broken — NVS write key does not match read key~~ (resolved)

`PrefsKeys.h:57` defined `SYSLOG_ENABLED = "syslog_enabled"`, but
`Config.cpp:558` and `Config.cpp:569` used the literal `"syslog_on"`. Result:
enabling syslog via the API wrote to a key nobody read; the device booted with
syslog off every time, with no error. `PrefsKeys::SYSLOG_ENABLED` itself was
unused.

**Resolution.**
- `src/Config.cpp:553-575` — `loadSyslogConfig()` and `saveSyslogConfig()`
  now use `PrefsKeys::SYSLOG_ENABLED`, `PrefsKeys::SYSLOG_PORT`,
  `PrefsKeys::SYSLOG_HOST`, and `PrefsKeys::NAMESPACE` (the prior literal
  strings and the duplicate `NAMESPACE` reference are gone).
- `src/PrefsKeys.h:60-69` — added
  `static_assert(Config::nvsKeyFits(...))` on the three syslog keys, matching
  the discipline in `Config.h:323-339`. A future over-length rename would
  now be a compile error rather than a silent write-only key.
- **Migration note for deployed devices:** syslog state written by the buggy
  save path lives under `"syslog_on"` and is now ignored. Users who had
  enabled syslog must toggle it off and on again (or do a settings save) once
  after the upgrade for the value to be persisted under the corrected key.

### 2. ~~`getTemperature()` does not average across multiple sensors — contradicts the spec~~ (resolved)

The original review flagged that `getTemperature()` does not average across
multiple sensors and that the spec required averaging. **Both observations
were correct, but the spec was wrong, not the code.** Averaging a −40 °C
faulty reading with a healthy one gives ~−9 °C, which is still a contaminated
value — averaging does not actually defend against sensor faults. The
defence belongs at the driver boundary (per-driver range validation), not
in the controller's accessor.

**Resolution.**
- `openspec/specs/sensor-management/spec.md` "SensorController aggregation"
  requirement updated to document first-sensor-wins (the actual behaviour),
  with the rationale recorded.
- A new requirement "Per-driver range validation" added to the same spec,
  directing each driver to reject out-of-range / NaN / Inf values before they
  reach `getMeasurements()`. See finding #24 for the corresponding code work.
- `AGENTS.md` and `README.md` updated to match (the "automatic averaging"
  claim is removed everywhere).

---

## HIGH severity

### 3. ~~`reserveSensorSlots()` is called too late to do its job~~ (resolved)

`main.cpp:265` runs `sensorController.begin()` (which itself `push_back`s a
`DeviceSensor` at `SensorController.cpp:91`), then the I2C scan loop adds
sensors at `main.cpp:228-247`, and only then is `reserveSensorSlots(MAX_KNOWN_SENSORS)`
called at `main.cpp:269`. The documented contract (`SensorController.h:170-176`)
says the reserve must precede the scan loop. The native test at
`test_memory_singleton_lifetimes:37` does not catch this because the test
fixture calls `reserve()` *before* `addSensor()` in the right order.

**Fix.** Move `reserveSensorSlots()` to run *before* the I2C scan loop (or
 document that `begin()` doesn't require pre-reservation). Move `DeviceSensor`
 addition out of `begin()` if strict contract fidelity is desired.

**Resolution.**
- `src/main.cpp:206` now calls `reserveSensorSlots(MAX_KNOWN_SENSORS)` before the I2C sensor assignment block and before `sensorController.begin()`.
- Verified with `pio test -e native` (460 tests passed) and `pio run -e adafruit_qtpy_esp32s2` (success).

### 4. `Support::Stats` is shared across tasks without synchronization (torn-read data race)

`Stats::add()` runs on the Sensor Monitor task at `task/SensorMonitor.cpp:85`;
`get_min()`, `get_max()`, `get_average()`, `get_count()` are called from the
AsyncTCP task at `routes/StatusRoutes.cpp:98-101`. All four fields are
`uint64_t` — 64-bit aligned reads can tear on ESP32 across the two 32-bit bus
cycles. `min` and `max` are particularly susceptible because they're updated
via non-atomic compare-store.

**Fix.** Snapshot under a short mutex, or use `std::atomic<uint32_t>` (the
values are cycle durations in ms — 49-day max fits comfortably).

### 5. ~~`deviceConfig` is a multi-task shared struct with no synchronization~~ (resolved)

`Config::ConfigManager::deviceConfig` (~250 B, multiple `char[]`, floats, etc.)
is written without locking by every `updateXxx()` from the AsyncTCP task
(`Config.cpp:244-345`) and read without locking from the Sensor Monitor task
inside `updateControl()` (`SensorController.cpp:552,659`). Single 32-bit
aligned fields don't tear, but a multi-field read on the Sensor Monitor task
can see a mix of old and new — exactly the race the project already solved
for PID gains via `pendingGains` / `gainsChangeRequested` at
`SensorController.h:113-114`.

**Resolution.**

- `src/Config.h:344-365` — new `deviceConfigLock` spinlock plus
  `lockDeviceConfig()` / `unlockDeviceConfig()` next to the existing
  `restartLock` precedent; `getDeviceConfigSnapshot()` returns an indivisible
  `DeviceConfig` copy taken under that lock.
- `src/Config.cpp` — every `updateXxx()` method, `loadDeviceConfig()` and
  `saveDeviceConfig()` now wraps its cache write(s) in
  `lockDeviceConfig()` / `unlockDeviceConfig()`. The NVS write stays
  outside the lock so a Settings-save does not stall cross-task readers
  for the duration of the flash commit.
- `src/SensorController.cpp` — `updateControl()` takes one
  `const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot()` at
  the top and reads every field from `cfg`; `isHeatingPermitted()` does the
  same for `temperature_control_enabled`.
- `src/Network.cpp:586` — `heatingActuator.configure(...)` now binds a
  snapshot local before the call, so `configure()` reads the host and
  channel as a single indivisible pair.
- Spec requirements added under `configuration` ("DeviceConfig snapshots
  are indivisible") and `system-architecture` ("Cross-task DeviceConfig
  reads use the snapshot accessor"). See change
  `fix-device-config-cross-task-race` for design and rationale.
- Verified with `pio test -e native` (465 tests passed, including a new
  5-case `test_device_config_snapshot` that drives concurrent writers and
  readers and asserts every snapshot matches a committed pair) and
  `pio run -e adafruit_qtpy_esp32s2` (success, same flash / RAM shape as
  baseline).

### 6. `/api/actuator` accepts arbitrary host string — SSRF on the LAN

`routes/ControlRoutes.cpp:298-329` accepts any string in `host`, copies it
into a 64-byte slot, and the URL is then constructed at
`HeatingActuator.cpp:38` and probed every 30 s. No hostname/IPv4/URL-injection
validation. Anyone on the LAN can force the device to issue HTTP GETs to
internal services and read back the response via `GET /api/actuator`.

**Fix.** Validate `^[A-Za-z0-9._-]{1,253}$` or a clean IPv4 literal; reject
`/`, `?`, `#`, `@`, whitespace.

### 7. XSS via `innerHTML` of OTA error / version fields in the web UI

`data/settings.html:868`, `:874`, `:982` insert `data.error` and
`data.latest_version` into the DOM via `innerHTML` without escaping. A LAN
attacker who can MITM the GitHub response (DNS poisoning) or a malicious
release tag can run script in the user's browser session.

**Fix.** Use `textContent` or `esc()` (already defined in
`data/about.html:57-61`).

### 8. Native test `build_src_filter` excludes ~14 test directories — `pio test -e native` reports green without running them

`platformio.ini:16` includes 14 of 28 test directories. Missing:
`test_actuator_report`, `test_mqtt_client`, `test_relay_autotuner`,
`test_request_diag`, `test_sensor_controller`, `test_sensor_base`,
`test_sensor_utils`, `test_shelly_channel`, `test_sht4x`, `test_status_led`
(only `test_status_led_error` is in the filter), `test_support`,
`test_temperature_control`, `test_tpo`. The temperature-control test suite
(~685 lines, 30+ cases) is silently dropped.

**Fix.** Add the missing `+<test/test_X/>` entries, or invert the filter to
exclude-only.

### 9. Network task uses heap-allocated stack — inconsistent with OTA's static-stack hardening

`Network::startTask()` (`Network.cpp:985-993`) uses `xTaskCreate()` with a
heap stack. The OTA parked tasks (`OTAUpdater.cpp:546-553`) use
`xTaskCreateStatic` with BSS-reserved stacks, justified by the comment at
`SensorMonitor.cpp:22-24`: a FreeRTOS stack must be in *contiguous internal
SRAM*, which `xTaskCreate()` may fail to provide once WiFi/mbedTLS have
fragmented the heap. The Network task is larger and more critical than the OTA
ones, yet it is the one still using the fragile path.

**Fix.** Migrate `Network::startTask()` to `xTaskCreateStatic`, matching the
OTA pattern.

### 10. Spec/code mismatch on task stack sizes — OpenSpec claims sizes the code no longer satisfies

`openspec/specs/system-architecture/spec.md:46,50` require Network task ≥ 14 KB
and SensorMonitor ≥ 8 KB. Actual: 8192 B (`Network.cpp:988`) and 6144 B
(`SensorMonitor.cpp:33`) — both justified with measured high-water marks and
headroom. The code is right; the spec is stale. Spec validation cannot catch
this because the spec is self-consistent.

**Fix.** Run `/opsx:propose` to lower the spec values and archive, with the
measured-justification comments preserved in the design doc.

### 11. AGENTS.md self-contradiction + README/spec wrong on PSRAM

`Network.cpp:555` records on-device: `psram_size 2094735` (~2 MB).
`AGENTS.md:460` correctly says "320 KB internal + 2 MB PSRAM", but
`AGENTS.md:492` says "**No PSRAM**: Adafruit QT Py ESP32-S2 board has no
PSRAM"; `README.md:28,374` and `openspec/specs/system-architecture/spec.md:8,13`
all say "no PSRAM" or "PSRAM SHALL NOT be assumed available".

**Fix.** Pick one truth. If the device has PSRAM, update `AGENTS.md:492`,
`README.md`, and the spec to match; document why task stacks remain
internal-only.

### 12. StatusLed enum contradicts AGENTS.md (documentation references non-existent API)

`LedState` (`src/StatusLed.h:17-23`) is `OFF, ON, STARTUP, TRANSMIT_DATA,
ERROR`. `AGENTS.md:272-278` references `LedState::MEASURING`,
`LedState::BLINK_SLOW`, `LedState::PULSE`, and convenience methods
`setMeasuring()` / `setNormal()` — none of which exist.

**Fix.** Either implement the documented states (yellow measuring / blue AP /
red pulse) or update `AGENTS.md` to match the shipped enum.

### 13. OpenSpec specs contradict each other on `WebServerManager` ownership

`openspec/specs/system-architecture/spec.md:91` says Network *owns*
`WebServerManager`; `openspec/specs/memory-management/spec.md:11-43` and the
code (`main.cpp:137`) construct it once at file scope and reuse. The
`system-architecture` spec is wrong; needs an archive change.

---

## MEDIUM severity

### 14. `volatile` scalars shared with WiFi event task are not actually atomic for `++`

`Network::reportInternetFailure()` (`Network.cpp:958-962`) and the comment at
`Network.h:84-96` market `volatile uint32_t++` as "atomic on ESP32". The
comment is wrong: `volatile ++` is load+add+store and not atomic by the C++
memory model. Today both fields are written and read from the Network task
itself, so the race is latent — but the comment invites future misuse.

**Fix.** Use `std::atomic<uint32_t>` (lock-free for 32-bit on Xtensa) or
commit to single-task ownership and drop the `volatile`.

### 15. `actuatorAssigned` / `actuatorAgreement` / `lastControlOutput` triplet read non-atomically across tasks

`publishActuatorState()` (`SensorController.h:278-281`, called from the Network
task) and `getReportedState()` (called from the AsyncTCP task via `/api/control`)
read the three fields without a snapshot. `getReportedState()` can report
`Heating` from `lastControlOutput` while `actuatorAgreement` describes an
earlier actuator cycle.

**Fix.** Snapshot under `dataMutex`, or split into `std::atomic<bool>` /
`std::atomic<uint32_t>` (bit-cast `float`).

### 16. `/api/status` and `/api/measurements` use TOCTOU — exactly the pattern `Snapshot` was created to fix

`routes/StatusRoutes.cpp:39-48,156-176` reads `isDataValid()` then
`getTemperature()` / `getRelativeHumidity()` / `getDewPoint()` as four
separate mutex-protected calls. `SensorController.h:153-158` documents exactly
this anti-pattern and introduces `Snapshot` as the replacement. `/api/sensors`
(`SensorRoutes.cpp:139`) correctly uses `getSnapshot()`; the two above routes
do not.

**Fix.** Replace the four-call pattern with `getSnapshot()`.

### 17. No upper bound on incoming request body length

Routes like `SettingsRoutes.cpp:21-67`, `ControlRoutes.cpp:298-329`,
`MqttRoutes.cpp:53-83`, `SensorRoutes.cpp:71-114`, and
`WebServerManager.cpp:139-201` consume the body chunk-by-chunk but never bound
its total size. A ~64 KB POST causes ArduinoJson to allocate through the
default heap allocator (internal SRAM only) — directly the heap-fragmentation
scenario the project otherwise guards against (`Network.cpp:611-614`).

**Fix.** Cap `request->contentLength()` per route (e.g. 2 KB for `/api/wifi`,
1 KB for settings, 256 B for syslog) and respond `413` if exceeded.

### 18. Heating-actuator host probed over plaintext HTTP — MITM can flip the relay

`HeatingActuator.cpp:38` does `snprintf("http://%s%s", host, path)`. Combined
with finding #6 (user-controlled host) and #19 (open AP), an attacker
ARP-spoofs the Shelly and injects `output:true` readings the controller
trusts to commit heat. There is no integrity check on responses.

**Fix.** Document the trust assumption; consider pinning the Shelly by MAC or
requiring local signing.

### 19. Configuration AP is open (no WPA2)

`Network.cpp:140` calls `WiFi.softAP(ap_ssid.c_str())` with no password.
Anyone in radio range can associate and `POST /api/wifi` with
attacker-controlled credentials, then have the device join an
attacker-controlled SSID.

**Fix.** Enable WPA2-PSK on the config AP (password derived from device id
and printed on the case), or require a physical button press.

### 20. E-paper device name is cached at `begin()` and never refreshes

`DisplayManager.cpp:46-47` caches `deviceName[32]` once.
`routes/SettingsRoutes.cpp:97` updates NVS and the in-memory cache but never
pushes to the display. After a UI rename, the panel keeps showing the old
name until reboot.

**Fix.** Have `updateDeviceName()` push to `DisplayManager`, and have
`render()` re-read from `config.getDeviceConfig()`.

### 21. mDNS instance name not refreshed on device-name change

`Network.cpp:94-114` only re-runs `configureMDNS()` on boot or WiFi reconnect.
After a settings-driven rename, the mDNS instance stays stale until WiFi
reconnects.

**Fix.** Call `configureMDNS()` (or just `MDNS.setInstanceName()`) from
`updateDeviceName()`.

### 22. Heating actuator has no emergency-stop after repeated `sendSet` failures

`HeatingActuator.cpp:127-141` and `:255-271`: when `sendSet()` fails,
`commanded` is *not* updated, and the actuator keeps believing the relay is
in its previous state until the 5-minute conformance re-check. During the
gap, `Agreement` can report `HeatingOk` while the relay is physically closed.
No backoff, no fault escalation, no LED indication.

**Fix.** After N consecutive `sendSet` failures within `2 × RENEW_MS`, force
`commanded = false`, downgrade `lastConformance`, and surface a fault.

### 23. Display fault state is invisible to the web UI

`EPaperDisplay.cpp:280-282` sets `faulted = true` after 3 BUSY timeouts;
`DisplayManager.cpp:104` silently skips updates. No HTTP endpoint exposes
`hasFaulted()`, so the user has no way to learn the panel is showing stale
data.

**Fix.** Add `faulted` to `/api/display` (spec already anticipates this at
`openspec/specs/display/spec.md:563-564`).

### 24. Most sensor drivers accept any value from the silicon — no range validation

Only `SHT4x.cpp:47` screens temperature/humidity plausibly. Every other
driver (`BME680`, `BMP3xx`, `SCD4x`, `SGP40`, `VEML7700`, `DPS310`, `PM25`,
etc.) accepts whatever the chip hands back. Combined with finding #2, a
single faulty sensor on a multi-sensor board dominates the loop with no
rejection.

**Fix.** Introduce a centralized range-validation step in
`SensorController::readSensors()` before measurements are appended.

### 25. `I2CBus::Lock` defaults to `portMAX_DELAY`

`I2CBus.h:86`. Any caller that writes `I2CBus::Lock bus;` (which parses as a
function declaration!) and then operates on it deadlocks the AsyncTCP handler
if the Sensor Monitor task is stuck. Both current call sites pass an explicit
timeout.

**Fix.** Remove the default, or set it to a short bound.

### 26. `xSemaphoreTake(..., portMAX_DELAY)` in `disableAndClear()`

`DisplayManager.cpp:72` uses `portMAX_DELAY` while holding the `panelMutex`
and waiting for the Network task to finish a repaint. If the e-paper BUSY
line is stuck, this hangs the AsyncTCP task indefinitely — every subsequent
web request stalls.

**Fix.** Use a bounded timeout and surface a clear error.

### 27. WiFi credentials logged at INFO and forwarded to syslog when syslog is enabled

`SettingsRoutes.cpp:59`, `WebServerManager.cpp:182` log "WiFi credentials
updated: SSID=%s". The `_KLIMA_LOG` macro at `Log.h:32-41` forwards to syslog,
sending the SSID off-device.

**Fix.** Demote to `ESP_LOGD`, or strip the SSID from the log line.

### 28. MQTT credentials in plaintext NVS; no TLS option

`Config.cpp:484,503` store `username`/`password` in NVS without flash
encryption. `MqttClient.cpp:14-15` uses `WiFiClient` (not
`WiFiClientSecure`). All MQTT traffic — credentials and topic contents —
traverses the LAN in cleartext.

**Fix.** Either document the threat model or add a TLS mode.

### 29. Syslog has no rate limit / no sensitive-data filter

`SyslogOutput.cpp:55-79` forwards every log line via UDP with no per-tag
token bucket. A noisy component floods the syslog receiver. Combined with
finding #27, sensitive strings leave the device.

**Fix.** Bound syslog send rate; redact SSIDs.

### 30. Inconsistent NVS key usage — literal strings instead of `PrefsKeys` constants

`Config.cpp:60,69,180,411,442,480-486,558-561` use raw literals
(`"device_name"`, `"wifi_failures"`, `"sns_assign"`, `"mqtt_enabled"`,
`"syslog_on"`, …) when `PrefsKeys.h` already defines named constants.
Renaming a constant would silently desync. **Finding #1 is a direct
consequence of this.**

**Fix.** Replace every literal with the `PrefsKeys::` constant; pair with
`static_assert(std::string_view(PrefsKeys::X) == "x")`.

### 31. Two NVS-namespace names with the same value, used inconsistently

`Config.h:293` has `NAMESPACE = "klima"` and `PrefsKeys.h:17` has `NAMESPACE =
"klima"`. `Config.cpp` mixes both within the same file. Today they happen to
coincide.

**Fix.** Route everything through `PrefsKeys::NAMESPACE`.

### 32. OTA download code is not covered by tests

`test_ota_updater` (115 lines) exercises `VersionCompare` only. The 600-line
firmware-download-and-flash path (`checkForUpdate`, `performUpdate`,
asset-name matching, host allowlist, redirect enforcement) has no native
coverage. The whole OTA safety story is unverified by CI.

**Fix.** Extract the pure-C++ helpers (`assetNameMatches`,
`isAllowedRedirectHost`, etc.) into a header that can be linked from native
tests.

### 33. Network failure paths not unit-tested

WiFi retry/backoff (`Network.cpp:182-326`), AP fallback
(`Network.cpp:342-382`), and active reconnect backstop
(`Network.cpp:660-714`) are all `#ifdef ARDUINO`-gated with no native
coverage.

**Fix.** Move backoff math out (already partially done via `WifiBackoff.cpp`)
and test it.

### 34. Sensor failure mid-control-cycle not tested

`test_temperature_control:308-512` tests `PidController` and a stand-in
`ControlLoop`, but explicitly does *not* test
`SensorController::updateControl()` itself (the comment at line 312-314
explains why). The integration boundary — autotune start/cancel/abort races,
`gainsChangeRequested` adoption, safety-shutoff hysteresis — is only verified
on hardware.

**Fix.** Add a `test_sensor_controller` that drives the real
`updateControl()` with mock sensors.

### 35. `I2CBus::recover()` retries every 3 s indefinitely on a wedged bus

`SensorController.cpp:264-284`. If `recover()` returns false,
`consecutiveI2CFailures` is reset to 0, so the device hammers the recovery
code at ~3 s intervals forever with a stuck bus.

**Fix.** Only reset the counter on successful recovery; back off the retry
interval after repeated failures.

### 36. `ShellyChannel::checkConformance` accepts `auto_off_delay = 0`

`ShellyChannel.cpp:113-133` requires `autoOffDelayS >= minAutoOffDelayS` but
doesn't separately require `> 0`. A Shelly configured with
`auto_off_delay = 0` (which the Shelly silently treats as immediate auto-off)
passes conformance — any single missed renewal instantly closes the valve.

**Fix.** Add `if (config.autoOffDelayS <= 0.0f) return AutoOffTooShort;`
before the existing check.

### 37. Syslog port/host input not validated

`SyslogRoutes.cpp:31-65` accepts any `port` 0–65535 and any `host`
(truncated to 64 bytes via `strlcpy`).

**Fix.** Reject `port == 0`; reject `host` not matching
`^[A-Za-z0-9._-]{1,253}$`.

### 38. mDNS `/etc/hosts` `device_name` not character-whitelisted

`Network.cpp:101-113` concatenates the user-controlled `device_name` into the
mDNS instance name without charset filtering.

**Fix.** Validate `^[A-Za-z0-9 ._-]{1,32}$` in `updateDeviceName()`.

### 39. `SyslogOutput::active` is `volatile` and read without synchronization

`SyslogOutput.h:31`, `SyslogOutput.cpp:11,55-56`. `send()` checks `active`
outside the mutex that protects the UDP write. A `setConfig()` that flips
`enabled` can race against an in-flight `send()`.

**Fix.** Use `std::atomic<bool>` for `active` and snapshot under the mutex.

### 40. No `OPTIONS` / CORS boundary response

Routes rely on the browser's preflight rejection. A direct `curl` from an
unauthorized origin gets exactly the same response as the legit UI — no
allowlist logging or defence-in-depth.

**Fix.** Register `server.on("/*", HTTP_OPTIONS, …)` returning 403.

### 41. `/api/about` exposes full MAC, SSID, BSSID, IP without auth

`routes/StatusRoutes.cpp:80-149`. Enables passive device fingerprinting on
the LAN.

**Fix.** Require `X-Requested-With` on diagnostic endpoints; consider masking
OUI bytes on `/api/about`.

### 42. `/api/diag/requests` exposes recent request log to any caller

`routes/ControlRoutes.cpp:399-426`. Returns client IPs, request bodies
(content-type + length) for the last N requests.

**Fix.** Gate behind a `diag.enabled` flag, default off.

### 43. `delay()` used in setup path blocks before tasks exist

`main.cpp:182`, `SCD4x.cpp:23`, `SensorController.cpp:66` all use `delay()`
rather than `vTaskDelay()`. Acceptable pre-task; worth a comment so
reviewers don't keep asking.

---

## LOW severity (worth fixing when convenient)

### 44. `Network.h:84-96` "32-bit aligned scalars are atomic on ESP32, so volatile is sufficient"

Same misconception as #14, but for plain scalars — the comment overstates
what `volatile` provides.

### 45. Magic numbers: `MIN_FREE_INTERNAL` vs `MIN_FREE_INTERNAL_BYTES`

`MIN_FREE_INTERNAL` (OTA gate, 20480 B) and `MIN_FREE_INTERNAL_BYTES`
(Network restart, 16384 B) — different names for different thresholds invite
accidental swap.

### 46. C-style casts scattered

`main.cpp:224`, `routes/SensorRoutes.cpp:34`, `OTAUpdater.cpp:407,417`,
`SensorController.cpp:254`, `sensor/SGP40.cpp:69`, `sensor/DeviceSensor.cpp:28-29`,
`SyslogOutput.cpp:69`. The codebase overwhelmingly uses `static_cast`.

### 47. `MAX_KNOWN_SENSORS` duplicated as `RESERVE_N`

`main.cpp:121` defines `MAX_KNOWN_SENSORS = 10`; `test_memory_singleton_lifetimes:26`
defines `RESERVE_N = 10`. Bumping one silently degrades the other's guarantee.

### 48. Two `getDeviceId()` implementations

`DeviceId.h:18-29` (used everywhere) and `Config.cpp:393-406` (called once
from `loadDeviceConfig()`). Delete the duplicate.

### 49. Measurement-capacity test asserts weaker bound than documented contract

`test_memory_singleton_lifetimes:57-69` asserts `>= RESERVE_N` (10) instead
of the documented `n * MAX_MEASUREMENTS_PER_SENSOR` (80).

### 50. `networkTaskHandle` global at `main.cpp:123` is dead code

Declared but never used; `Network` class has its own `taskHandle`.

### 51. `NetworkMode mode` field is set but never read

`Network.h:51,204`, `Network.cpp:62,132,184` — `getMode()` has no callers.

### 52. TPO open-pulse snap is asymmetric

`TimeProportionalOutput.cpp:83-89` checks `cycleMs - requested < travelMs`
but not the symmetric `requested < travelMs` — a very short pulse can
represent half a stroke and leave the valve mid-travel.

### 53. Anti-windup is "clamping", not "back-calculation"

`PidController.cpp:38` — saturated output still accumulates integral. Worth
documenting.

### 54. Derivative is on error, not on measurement

`PidController.cpp:44-47` — setpoint changes cause a derivative kick.
Harmless with the shipped `kd = 0` default; surfaces if a user sets
`kd > 0`.

### 55. `AutotuneLimits::requiredCycles` and `minAmplitudeRatio` have no validator

`requiredCycles < 3` and `minAmplitudeRatio <= 1.0` produce nonsense.

### 56. Autotuner's `consistentCycles` semantics

`RelayAutotuner.cpp:101-106` jumps to 2 on first consistent pair —
"three cycles required" is actually "two measured periods"; document the
semantics in the spec.

### 57. Autotuner has no test for a plant that never oscillates

Or for a plant faster than the sensor cadence. Both are real failure paths.

### 58. mDNS `MDNS.end()` not called before `MDNS.begin()` on AP↔STA transitions

`Network.cpp:94,145,284,638` — relies on the library's idempotency.

### 59. Logging level: routine state changes at INFO

`MqttClient.cpp:84,133`, `routes/SettingsRoutes.cpp:59,99,136` should be
`ESP_LOGD`.

### 60. `catch(...)` blocks in `main.cpp:254,338,347`

Only catch `bad_alloc` from `make_unique`; they log and continue, masking
memory exhaustion during setup. Consider LED + restart instead.

### 61. Two `NAMESPACE` constants in different headers

Same string `"klima"`, but `Config.h:293` and `PrefsKeys.h:17` declare them
independently. See #31.

### 62. `Stats::get_max()` returns 0 when no samples

Ambiguous with a real zero reading; callers must use `get_count()` to
disambiguate.

### 63. `PrefsKeys.h` constants are not enforced by `static_assert(nvsKeyFits(...))`

`Config.h:323-339` does the right thing on its own keys; `PrefsKeys.h` does
not.

### 64. `Preferences::getString` heap-allocating temporary `String` in some routes

`Config.cpp:180-185, 442-444` — inconsistent with the "no heap allocation
for config reads" philosophy.

### 65. Web UI `fetch()` calls have no `AbortController` / timeout

A hung connection leaves the UI in `Loading…` for 60+ s; the 10 s
`setInterval` keeps firing (`data/control.html:499-503`).

### 66. `setInterval` keeps polling even when `document.hidden`

Wastes the ESP's TCP socket pool across multiple open tabs.

### 67. Mobile `control-bar` has no `flex-wrap`

`data/common.css:249-254` — cramps below ~340 px.

### 68. No progress bar during OTA download

Just a percent text (`data/settings.html:937-996`).

### 69. `alert()` for settings save errors is intrusive

The control page already has an inline pattern; settings should adopt it.

### 70. `saveDeviceConfig` ignores `Preferences::putFloat` return value

Silent flash failure — should log a warning and increment a counter.

---

## Positive findings (verified)

- **JsonDocument discipline.** Zero `make_unique<JsonDocument>` /
  `new JsonDocument` / `DynamicJsonDocument` matches across `src/`. The
  stack-frame rule is correctly enforced.
- **OTA security story.** HTTPS + `esp_crt_bundle` + exact asset name
  (`firmware.bin`, not `.bin` suffix) + host allowlist + strict
  version-compare + memory gate + `confirmRunningImage()` last in setup. All
  eight properties claimed in `AGENTS.md` are implemented as described.
- **Parked OTA tasks.** `vTaskDelete` is never called; OTA tasks use
  `xTaskCreateStatic` with BSS-reserved stacks; mutual exclusion via a single
  `std::atomic<Activity>` with compare-exchange. Verified correct.
- **Password handling.** Passwords are not logged anywhere — only SSIDs, and
  only at INFO when settings change.
- **Stack canaries.** `-fstack-protector` confirmed in `compile_commands.json`.
- **NVS key length guard.** `Config.h:323-339` uses
  `static_assert(nvsKeyFits(...))` on every key — exactly the right
  discipline; just needs the same enforcement on `PrefsKeys.h` (see #63).
- **RefreshPolicy.** Hysteresis on values, hysteresis on demand bucket,
  periodic full-refresh promotion (`RefreshPolicy.cpp:155-168`) — robust.
- **Display splash path.** Clean; watchdog fed both before and after the page
  loop.
- **TWDT subscription order.** TWDT init in setup before tasks start; each
  task calls `esp_task_wdt_add(NULL)` as its first act
  (`main.cpp:294-311`, `Network.cpp`, `SensorMonitor.cpp:53-56`) — verified
  correct.
- **Core dump handling.** Stored in `coredump` partition, deliberately not
  erased so `espcoredump.py` can pull the full ELF (`main.cpp:71-79`).

---

## Recommended priority order

1. ~~**#1** syslog NVS key mismatch — functional bug, ~2-line fix.~~ Resolved.
2. ~~**#2** sensor averaging — spec violation~~ — resolved by correcting the
   spec (averaging was the wrong defence); the real work is #24.
3. ~~**#5** `deviceConfig` cross-task read — same pattern the project already
   solved for gains.~~ Resolved.
4. ~~**#3** `reserveSensorSlots` ordering — documented contract not honored.~~
5. **#4** `Stats` race — `/api/about` reads torn 64-bit values today.
6. **#7** XSS in OTA UI — straightforward `textContent` substitution.
7. **#8** test filter excludes ~half the suite — `pio test -e native`
   silently green.
8. **#10**, **#11**, **#12**, **#13** — documentation/spec drift
   corrections, cheap.
9. **#6**, **#19** — SSRF + open AP together make LAN-side reconfiguration
   a single POST away.
10. **#16**, **#17** — TOCTOU and unbounded body sizes in the API.
11. **#22**, **#36** — actuator fault detection hardening.
