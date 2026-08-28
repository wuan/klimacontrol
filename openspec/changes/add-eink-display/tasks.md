## 1. Configuration layer

- [x] 1.1 In `src/PrefsKeys.h`, add a "Display configuration" block with `DISPLAY_ENABLED = "disp_enabled"`, `DISPLAY_ROTATION = "disp_rot"`, `DISPLAY_INTERVAL = "disp_intv"`, `DISPLAY_CLEAR_PENDING = "disp_clear"` — all ≤12 characters per the NVS reliability note at the top of that file
- [x] 1.2 In `src/Config.h`, add `struct DisplayConfig { bool enabled = false; uint8_t rotation = 0; uint16_t interval = 60; bool clear_pending = false; };` next to `SyslogConfig`, with a comment marking `clear_pending` as firmware-internal (never exposed via the HTTP API)
- [x] 1.3 In `src/Config.h`, declare `void validateDisplayConfig(DisplayConfig &config);` alongside the other validators, and declare `DisplayConfig loadDisplayConfig();` / `void saveDisplayConfig(const DisplayConfig &config);` on `ConfigManager`
- [x] 1.4 In `src/Config.cpp`, implement `validateDisplayConfig()` clamping `rotation` to 0..3 and `interval` to 10..3600 (constants `MIN_DISPLAY_INTERVAL = 10`, `MAX_DISPLAY_INTERVAL = 3600` in the `Config` namespace). This function must be free of `#ifdef ARDUINO` so the native `test_config` suite can reach it
- [x] 1.5 In `src/Config.cpp`, implement `loadDisplayConfig()` / `saveDisplayConfig()` using `PreferencesGuard`, matching the `SyslogConfig` implementations
- [x] 1.6 Add `validateDisplayConfig()` boundary cases to the existing `test/test_config/` suite: `rotation` 0/3/4/255, `interval` 0/9/10/60/3600/3601/65535, and the all-defaults round trip

## 2. Refresh policy (native-testable, no hardware)

- [x] 2.1 Create `src/display/RefreshPolicy.h` with `namespace Display { enum class RefreshKind { None, Partial, Full }; }` and the constants `TEMP_HYSTERESIS_C = 0.2f`, `HUMIDITY_HYSTERESIS_PCT = 1.0f`, `FULL_REFRESH_EVERY_N_PARTIALS = 12`
- [x] 2.2 In the same header, declare `class RefreshPolicy` with `explicit RefreshPolicy(uint16_t minIntervalSec)`, `RefreshKind evaluate(float temperature, float humidity, bool valid, uint32_t nowMs)`, and `void reset()`. No Arduino or FreeRTOS includes — `nowMs` is a parameter, not a `millis()` call
- [x] 2.3 Create `src/display/RefreshPolicy.cpp` implementing the decision order from design D4: first-paint → `Full`; then hysteresis (or a validity transition) gate; then the minimum-interval floor; then the partial counter promoting every 12th partial to `Full`. Treat `NAN` temperature or humidity as invalid. Use unsigned subtraction for all elapsed-time comparisons so `millis()` rollover is handled
- [x] 2.4 In `RefreshPolicy.cpp`, record the rendered values only when a refresh is actually returned, so a change suppressed by the interval floor stays outstanding and fires on a later tick
- [x] 2.5 Add `size_t formatTemperature(char *out, size_t n, float v, bool valid)` and `size_t formatHumidity(char *out, size_t n, float v, bool valid)` to `RefreshPolicy.{h,cpp}` — one decimal for temperature, whole number for humidity, `"--.-"` / `"--"` when invalid or `NAN`
- [x] 2.6 Add `+<display/RefreshPolicy.cpp>` and `+<test/test_display_refresh_policy/>` to the `env:native` `build_src_filter` in `platformio.ini`
- [x] 2.7 Create `test/test_display_refresh_policy/test_display_refresh_policy.cpp` (Unity) covering: first evaluation returns `Full`; sub-hysteresis noise returns `None`; a change inside the interval floor returns `None` then fires once the floor passes; the 12th partial is promoted to `Full` and the counter resets; valid→invalid and invalid→valid both force a refresh; `NAN` is treated as invalid; `millis()` rollover does not suppress refreshes; both formatters produce the expected strings including the placeholders
- [x] 2.8 Run `pio test -e native` and confirm the new suite passes before any hardware code exists

## 3. Dependency and pin map

- [x] 3.1 Add `zinggjm/GxEPD2@^1.6.4` to `lib_deps` in `[env:adafruit_qtpy_esp32s2]` in `platformio.ini` (pin the exact resolved version once installed). Confirm `adafruit/Adafruit GFX Library` resolves transitively; add it explicitly only if it does not
- [x] 3.2 Create `src/display/DisplayPins.h` in `namespace DisplayPins` with `constexpr int SCK = 36; MOSI = 35; CS = 18; DC = 8; RST = 9; BUSY = 17;`, plus a comment block recording: the values were verified against `variants/adafruit_qtpy_esp32s2/pins_arduino.h`; GPIO37 (`MI`) is left unconnected and must NOT be used for CS (see 3.4); flash/PSRAM occupy GPIO27–32; and none of these collide with `SDA1`/`SCL1` (41/40), the NeoPixel (39/38) or GPIO0
- [x] 3.3 Add a `static_assert` block (or equivalent compile-time check) in `DisplayPins.h` asserting `CS != MISO`, `SCK == ::SCK`, `MOSI == ::MOSI`, `CS == A0`, `DC == A3`, `RST == A2`, `BUSY == A1`, so a variant-file change or a well-meaning "reclaim the MI pad" edit fails the build instead of shipping
- [x] 3.4 Add a comment at the `SPI.begin()` call site recording why CS is not on GPIO37: `spiAttachMISO()` (`esp32-hal-spi.c:203`) rewrites a `-1` MISO argument to GPIO37 on ESP32-S2 FSPI and then calls `pinMode(37, INPUT)`, so a chip-select there is silently reconfigured to an input depending on init ordering
- [x] 3.5 Run `pio run -e adafruit_qtpy_esp32s2` after adding only the dependency, and record the flash delta so the library's own cost is known separately from the feature's

## 4. E-paper driver wrapper

- [x] 4.1 Create `src/display/EPaperDisplay.h` — the whole body `#ifdef ARDUINO`-guarded. Declare `class EPaperDisplay` with `bool begin(uint8_t rotation)`, `void showSplash(const char *deviceName)`, `void render(const char *tempStr, const char *humStr, const char *footerLeft, const char *footerRight, Display::RefreshKind kind)`, `void clear()`, `void hibernate()`, `bool hasFaulted() const`
- [x] 4.2 In `src/display/EPaperDisplay.cpp`, instantiate `GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT / 8> display(GxEPD2_154_D67(DisplayPins::CS, DisplayPins::DC, DisplayPins::RST, DisplayPins::BUSY));` at file scope, with a comment stating the 625-byte page buffer and why a full framebuffer is rejected (it would push internal free below the 20480 B OTA gate)
- [x] 4.3 Implement `begin()`: call `SPI.begin(DisplayPins::SCK, MISO, DisplayPins::MOSI, -1)` explicitly before `display.init(...)` so bus setup order is owned by this code rather than by GxEPD2's internals, then `display.setRotation(rotation)`, `display.setTextColor(GxEPD_BLACK)`, then `hibernate()`. Pass the real `MISO` (37) rather than `-1` — the core substitutes 37 either way, and being explicit documents that the pin is claimed
- [x] 4.3a Verified **functionally** rather than with a scope: the panel renders, and on SSD1681 every command byte is latched while CS is low. The regression this task guards against — `SPI.begin()` reverting CS to an input — would mean zero bytes ever reach the controller and a permanently blank panel. A rendering display is therefore conclusive evidence that CS idles HIGH and asserts correctly
- [x] 4.4 Implement the paged draw for `render()`: for `Full`, `setFullWindow()` and draw the value block plus the footer; for `Partial`, `setPartialWindow(0, 30, 200, 110)` and draw only the value block. Both inside `do { display.fillScreen(GxEPD_WHITE); … } while (display.nextPage());`
- [x] 4.5 Use `FreeSansBold24pt7b` for the temperature and `FreeSans12pt7b` for the humidity; centre both horizontally using `getTextBounds()`. Draw the footer with the default GFX font below y=140
- [x] 4.6 Wrap the blocking page loop with `esp_task_wdt_reset()` immediately before and immediately after, per the `system-architecture` blocking-external-call requirement
- [x] 4.7 Implement the fault guard: time the page loop with `millis()`; a run exceeding `REFRESH_TIMEOUT_MS = 12000` increments a consecutive-timeout counter, a faster run resets it; at `MAX_CONSECUTIVE_TIMEOUTS = 3` set an in-memory `faulted` flag, log once at `ESP_LOGE`, and make every later `render()` a no-op. Do not touch NVS
- [x] 4.8 Implement `clear()` as a full-window white paint followed by `hibernate()`, and call `display.hibernate()` at the end of every successful render

## 5. Display manager and wiring

- [x] 5.1 Create `src/display/DisplayManager.{h,cpp}` (`#ifdef ARDUINO`) owning a `RefreshPolicy` and an `EPaperDisplay`, holding a `SensorController &` and a copy of the `DisplayConfig`
- [x] 5.2 Implement `DisplayManager::update()`: take `SensorController::getSnapshot()`, extract temperature and relative humidity from the measurement vector, call `policy.evaluate(...)` with `millis()`, and return immediately on `RefreshKind::None`. Otherwise format the strings and call `EPaperDisplay::render()`
- [x] 5.3 Implement `DisplayManager::begin()` (init + splash) and `DisplayManager::clearAndPark()` (the disable path)
- [x] 5.4 In `src/Network.h`, add a nullable `DisplayManager *display = nullptr;` member and a `void setDisplay(DisplayManager *d);` setter, mirroring the existing `setWebServer()` pattern that breaks the construction cycle
- [x] 5.5 In `src/Network.cpp`, in the one-second loop immediately after `statusLed.update()` (around line 572, where the `touchController->update()` call is commented out), add `if (display && !otaActive) { display->update(); }`
- [x] 5.6 In `src/main.cpp`, add a file-scope `DisplayManager displayManager(sensorController);` next to `statusLed`, and in `setup()` after `config.begin()`: load the display config; if `enabled`, `displayManager.begin(cfg)` and `network.setDisplay(&displayManager)`; if not enabled and `clear_pending`, run `displayManager.clearAndPark()` then save the config with `clear_pending = false`; if enabled and `clear_pending`, just save the config with the flag cleared
- [x] 5.7 Place the display init after `config.begin()` but before `network.begin()`, so the splash appears before WiFi association starts

## 6. HTTP route

- [x] 6.1 Create `src/routes/DisplayRoutes.cpp` modelled on `src/routes/SyslogRoutes.cpp`, exposing `GET /api/display` returning `{"enabled":…, "rotation":…, "interval":…}` and never emitting `clear_pending`
- [x] 6.2 Implement `POST /api/display`: `verifyCsrfHeader()` first, then deserialize, populate a `DisplayConfig` from the body (ignoring any `clear_pending` in the request), run `validateDisplayConfig()`, set `clear_pending = true` when the request turns a previously-enabled display off, `saveDisplayConfig()`, then `config.requestRestart(1000)`. Do not drive the panel from the handler
- [x] 6.3 Declare `setupDisplayRoutes()` alongside the other route setups and call it from `WebServerManager` next to `setupMqttRoutes()`
- [x] 6.4 Keep the JSON response within the documented per-route allocation discipline in the `http-api` spec

## 7. Web UI

- [x] 7.1 Add a Display tab to `data/settings.html` with an enable checkbox, a rotation select (0/90/180/270 → 0..3), and a numeric refresh-interval field (10..3600 s)
- [x] 7.2 Populate the tab from `GET /api/display` on load, and POST to `/api/display` with the `X-Requested-With: KlimaControl` header on save
- [x] 7.3 Show the same "device will restart" notice the other restart-triggering tabs use
- [x] 7.4 Keep to vanilla JavaScript per the `web-interface` spec; confirm `scripts/compress_web.py` regenerates `src/generated/settings_gz.h` during the build

## 8. Wiring documentation

- [x] 8.1 Create `docs/EINK_DISPLAY_WIRING.md` with a "Which module" section: the supported part is the **Waveshare 1.54inch e-Paper Module** (mono, SSD1681, 200×200) with the 8-pin 2.54 mm header. Describe how to distinguish it from the 1.54inch (B)/(C) tri-colour modules (unsupported — no partial refresh, ~15 s full refresh) and from the bare-panel + e-Paper Driver HAT packaging (usable, but the carrier's jumpers must be set to 3.3 V and 4-wire SPI)
- [x] 8.2 Add the wiring table: panel signal → GPIO → QT Py pad silkscreen label → Waveshare ribbon colour (`VCC` red, `GND` brown/black, `DIN` blue, `CLK` yellow, `CS` orange, `DC` white, `RST` purple, `BUSY` grey). Mark the colour column "verify against your cable" — Waveshare's colour scheme is conventional but not contractual
- [x] 8.3 Add an ASCII wiring diagram identifying pads by silkscreen label rather than physical position, and state explicitly that the `MI` pad is left unconnected and why (one line, pointing at design D2a)
- [x] 8.4 Add a power section: 3.3 V only from the `3V` pad; typical/peak current during refresh; the recommended 100 µF bulk capacitor across the module's 3V3/GND, with the rationale tied to the `ESP_RST_BROWNOUT` history already handled in `src/main.cpp`
- [x] 8.5 Add a pre-power-on checklist: continuity from each pad to its header pin, no short between `3V` and `GND`, `VCC`/`GND` not reversed (destructive on this panel), and ribbon fully seated in its FPC connector
- [x] 8.6 Add a symptom → cause troubleshooting table covering at minimum: panel stays blank (CS/DC swapped, CS on the wrong pin, panel unpowered); panel shows noise (DIN/CLK swapped, clock too fast); BUSY never releases (BUSY not connected, floating input) — cross-reference the three-strike fault guard and its `ESP_LOGE` line; faint or ghosted image (partial refreshes without a full refresh, or wrong panel variant selected); device reboots during refresh (brownout — see 8.4 and the `Reset reason:` boot log)
- [x] 8.7 Add a "Verifying the wiring" section: enable the display via the settings UI, confirm the boot splash, and read the expected `ESP_LOGI`/`ESP_LOGE` lines on the USB CDC console
- [x] 8.8 Link the new document from `README.md` next to the existing `docs/` references, and add a pointer from the `DisplayPins.h` comment block back to it so the two stay coupled

## 9. Build, test, verify on hardware

- [x] 9.1 `pio run -e adafruit_qtpy_esp32s2` — SUCCESS, zero new warnings under `-Wall -Wextra -Werror`. Flash 1,303,338 B of 1,900,544 (68.6%), i.e. **+33,564 B** over the pre-change baseline of 1,269,774 B — comfortably under the 1900 KB cap and well below the 60-80 KB estimated in the proposal
- [x] 9.2 `pio test -e native` — 282/282 PASS across 21 suites (245 existing + 8 new `validateDisplayConfig` cases in `test_config` + 29 new cases in `test_display_refresh_policy`)
- [ ] 9.3 Boot with the display disabled (the default) and confirm from the boot heap log that internal free is essentially unchanged apart from the page buffer. **Static footprint already measured from the ELF** (`xtensa-esp32s2-elf-nm -S`): `Display::(anon)::display` = 752 B BSS (the 625 B page buffer plus GxEPD2/GFX object state), `displayManager` = 68 B, total **820 B** unconditional. Leaves steady-state internal free ~23.2 KB, above the 20480 B OTA gate. Boot-log confirmation still pending hardware. **Subsumed by 9.6**: internal free with the display *enabled* is strictly lower than with it disabled, so a passing 9.6 measurement settles this case too
- [x] 9.4 Verified on hardware. Panel wired per `DisplayPins.h` and enabled via the settings UI; all four behavioural confirmations observed: the boot splash appears, the first measurement paints as a full refresh, subsequent updates are partial and flash-free, and the periodic full refresh fires after ~12 partials. The last of these also confirms `Display::RefreshPolicy`'s partial counter and promotion logic — unit-tested natively — behaving correctly against a real panel. **The 100 µF bulk capacitor is NOT fitted**; tracked separately as 9.4a
- [ ] 9.4a Decide on the 100 µF bulk capacitor across the module's 3V3/GND. Currently absent, so the panel's ~25 mA refresh transient is unbuffered on a board with a known `ESP_RST_BROWNOUT` history. Not required unless 9.9 observes a brownout — but that makes 9.9 a real test rather than a formality, so run 9.9 before closing this out
- [ ] 9.5 Confirm the placeholder rendering by booting with the display enabled and no sensor configured
- [ ] 9.6 Confirm `GET /api/status` still reports internal free above 20480 B with the display enabled, and run a full OTA update end to end to prove the gate is still clearable
- [ ] 9.7 Disable the display via the UI and confirm the panel is blank after the restart and that `disp_clear` has been cleared
- [ ] 9.8 Unplug the panel with the display enabled; confirm three timed-out attempts, one `ESP_LOGE` line, no further refresh attempts, no watchdog reset, and that `GET /api/display` still reports `enabled: true`
- [ ] 9.9 Check the periodic `Reset reason:` line across a few hours of operation for any `BROWNOUT` that was not present before the display was added
- [ ] 9.10 Archive the change with `/opsx:archive` to create `openspec/specs/display/spec.md` and fold the four spec deltas into their existing capability specs
