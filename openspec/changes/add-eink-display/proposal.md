## Why

The device currently has exactly one local output channel: the single built-in
NeoPixel driven by `StatusLed` (`src/StatusLed.cpp`), which can express five
coarse states (`OFF`, `ON`, `STARTUP`, `TRANSMIT_DATA`, `ERROR`) and an
MQTT-progress gradient. Reading the actual measured temperature or humidity
requires the web UI (`data/control.html` polling `/api/sensors`) or an MQTT
subscriber. For a device whose entire purpose is reporting room climate, there
is no way to glance at it and see the number.

A Waveshare 1.54" V2 e-paper module (200×200, SSD1681) is a good fit for this
board specifically:

- It holds its image with **zero power**, so a climate readout costs nothing
  between refreshes — unlike an OLED/TFT that must stay lit.
- It is write-only SPI, so it needs no MISO line and does not contend with the
  STEMMA QT I2C bus (`SDA1`/`SCL1` = GPIO41/40) the sensors already use.
- The QT Py ESP32-S2 breaks out hardware SPI (`SCK`=GPIO36, `MOSI`=GPIO35) plus
  enough spare pads for `CS`/`DC`/`RST`/`BUSY` with pins left over.

The constraint that shapes the design is internal SRAM. This board's internal
heap sits at roughly 24 KB free at steady state, the OTA pre-flight gate is
20480 B, and the Network task restarts the device below 16384 B — a history
already recorded in `src/OTAUpdater.h` and `src/Network.cpp`. GxEPD2's
framebuffer is a member array, so a full 200×200 buffer would put **5000 bytes**
into BSS and push steady-state internal free *below the OTA gate*, silently
disabling firmware updates. Paged rendering at 25 rows costs **625 bytes**
instead, which is noise against that budget, at the price of running the draw
callback 8 times per refresh — negligible for text.

## What Changes

- Add a new `display` capability: a runtime-configurable, **default-off**
  Waveshare 1.54" V2 (SSD1681) e-paper readout showing current temperature and
  relative humidity.
- Add `zinggjm/GxEPD2` (and its transitive `adafruit/Adafruit GFX Library`
  dependency) to the `adafruit_qtpy_esp32s2` env's `lib_deps` in
  `platformio.ini`. No new dependency for the `native` env.
- Add `src/display/DisplayPins.h` with **compile-time** pin constants:
  `CS`=GPIO18 (A0), `DC`=GPIO8 (A3), `RST`=GPIO9 (A2), `BUSY`=GPIO17 (A1);
  `SCK`=GPIO36 and `MOSI`=GPIO35 come from the default Arduino `SPI` object.
  CS deliberately does **not** reuse the electrically-free MISO pad (GPIO37):
  `SPI.begin()` on this target attaches MISO unconditionally and calls
  `pinMode(37, INPUT)`, which would clobber a chip-select configured there.
  Pins are not runtime-configurable — see design D2 and D2a.
- Add `docs/EINK_DISPLAY_WIRING.md`: a standalone hardware guide covering which
  Waveshare product to buy, the verified pin-by-pin wiring table, the Waveshare
  ribbon-cable colour map, power and decoupling, a pre-power-on continuity
  checklist, and a symptom-to-cause troubleshooting table. Linked from
  `README.md`.
- Add `src/display/RefreshPolicy.{h,cpp}` — a pure, `ARDUINO`-free decision
  function returning `RefreshKind::{None, Partial, Full}` from
  `(temperature, humidity, valid, nowMs)`, applying value hysteresis
  (±0.2 °C, ±1 %RH), a minimum-interval floor from config, and a
  full-refresh-every-N-partials ghosting counter. Added to the `native`
  `build_src_filter` with a `test/test_display_refresh_policy/` Unity suite.
- Add `src/display/EPaperDisplay.{h,cpp}` — `#ifdef ARDUINO` GxEPD2 wrapper
  declared as
  `GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT / 8>` (625 B page buffer,
  8 pages). Provides `begin()`, `showSplash()`, `render()`, `clear()`,
  `hibernate()`, `hasFaulted()`.
- Add `src/display/DisplayManager.{h,cpp}` — owns the `RefreshPolicy` and the
  `EPaperDisplay`, holds a `SensorController&`, exposes a single `update()`
  called once per second from the Network task loop.
- Add `Config::DisplayConfig` (`enabled`=false, `rotation`=0,
  `interval`=60 s, plus an internal `clear_pending` flag),
  `loadDisplayConfig()`/`saveDisplayConfig()`, `validateDisplayConfig()`, and
  the NVS keys `disp_enabled` / `disp_rot` / `disp_intv` / `disp_clear`. All
  keys are ≤12 characters per the `PrefsKeys.h` NVS-reliability rule.
- Add `src/routes/DisplayRoutes.cpp` exposing `GET /api/display` and
  `POST /api/display`, modelled on the existing `SyslogRoutes.cpp`. The POST
  route requires the `X-Requested-With: KlimaControl` CSRF header, persists the
  config, and calls `config.requestRestart(1000)` — matching the
  save-then-restart convention every other settings route already follows
  (`src/routes/SensorRoutes.cpp:109`, `src/routes/SettingsRoutes.cpp:62`).
- Add a **Display** tab to `data/settings.html` with an enable toggle, a
  rotation selector, and a refresh-interval field.
- **Clear-on-disable**: disabling the display sets a persistent `disp_clear`
  one-shot flag. On the next boot, if the display is disabled *and* the flag is
  set, the firmware initialises the panel, blanks it to white, hibernates, and
  clears the flag. Without this the panel would keep showing a stale reading
  forever, because e-paper retains its image unpowered.
- **Boot splash**: when enabled, the panel shows the device name and a
  "starting…" line at the end of `setup()`, so a booting device is visibly
  distinct from a dead one.
- **BUSY fault guard**: each refresh is wall-clock timed. Three consecutive
  refreshes exceeding the budget (panel unplugged, BUSY line stuck) put the
  display into an in-memory `faulted` state that stops further refresh attempts
  until reboot, logged at `ESP_LOGE`. The persisted config is **not** mutated —
  a hardware fault must not silently rewrite the user's settings. Because the
  refresh is a blocking external call on the Network task, `esp_task_wdt_reset()`
  is fed immediately before and after it, as the `system-architecture`
  *FreeRTOS task structure* requirement demands.

The display is off by default, so a device that is flashed and not reconfigured
behaves exactly as it does today apart from 625 B of BSS and the added flash.

## Capabilities

### New Capabilities

- `display`: hardware target and wiring, paged rendering discipline, the
  refresh policy (hysteresis / minimum interval / periodic full refresh),
  layout, boot splash, clear-on-disable, and the BUSY fault guard.

### Modified Capabilities

- `configuration`: the *Configuration structs* requirement gains
  `DisplayConfig` in its struct list, and a new scenario covers the
  `clear_pending` one-shot flag surviving a power cut mid-toggle.
- `http-api`: a new *Display endpoints* requirement for `GET`/`POST
  /api/display`, including the CSRF header and the restart-on-save behaviour.
- `web-interface`: the *Tabbed settings modal* requirement is widened from five
  named tabs to include Display.
- `system-architecture`: the *Task responsibilities* requirement is extended so
  the Network task's list of duties names the display refresh, and a scenario
  asserts the watchdog feed around the blocking refresh.

## Impact

- **New source files**:
  - `src/display/DisplayPins.h`, `src/display/RefreshPolicy.{h,cpp}`,
    `src/display/EPaperDisplay.{h,cpp}`, `src/display/DisplayManager.{h,cpp}`
  - `src/routes/DisplayRoutes.cpp`
  - `test/test_display_refresh_policy/test_display_refresh_policy.cpp`
- **Modified source files**:
  - `src/Config.h` / `src/Config.cpp` — `DisplayConfig`, load/save,
    `validateDisplayConfig()`
  - `src/PrefsKeys.h` — four new keys
  - `src/Network.h` / `src/Network.cpp` — nullable `DisplayManager*` wired via
    a `setDisplay()` call (mirroring the existing `setWebServer()` pattern that
    breaks the construction cycle), plus the `update()` call in the 1-second
    loop, skipped while `otaActive`
  - `src/WebServerManager.cpp` — register `setupDisplayRoutes()`
  - `src/main.cpp` — file-scope `DisplayManager`, config-gated `begin()`,
    splash, clear-on-disable handling
  - `data/settings.html` — Display tab
  - `README.md` — link to the new wiring guide
  - `platformio.ini` — `GxEPD2` dep; `+<display/RefreshPolicy.cpp>` and
    `+<test/test_display_refresh_policy/>` in the native `build_src_filter`
- **New documentation**: `docs/EINK_DISPLAY_WIRING.md` — the hardware-side
  companion to `DisplayPins.h`, written so someone with the board and the module
  in hand can wire it correctly without reading the firmware.
- **Spec files**: new `openspec/specs/display/spec.md`; text updates to
  `configuration`, `http-api`, `web-interface`, `system-architecture`.
- **Memory**: +625 B BSS (internal SRAM), unconditional — the page buffer is a
  member of a file-scope object and is not reclaimed when the display is
  disabled. This is ~2.6 % of the ~24 KB steady-state internal free and does
  not approach either the 20480 B OTA gate or the 16384 B restart threshold.
- **Flash**: GxEPD2 + Adafruit GFX + two GFX fonts, estimated 60–80 KB. Current
  firmware is 1,270 KB of the 1,856 KB app slot, leaving ~586 KB of headroom, so
  the `system-architecture` *Memory budget* 1900 KB cap is not at risk.
- **No API contract changes** to existing endpoints. `/api/status` and
  `/api/sensors` are untouched.
- **Out of scope**: rendering measurements other than temperature and humidity
  (CO₂, VOC, pressure, PM2.5) even though `SensorController` exposes them;
  tri-colour or non-200×200 Waveshare panels; runtime-configurable GPIO;
  displaying OTA progress; icons, bitmaps, or trend graphs; deep-sleep
  integration with `POWER_OPTIMIZATION.md`; driving the display from MQTT.
