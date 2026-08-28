## Context

The firmware's only local output is the built-in NeoPixel (`src/StatusLed.cpp`,
one `NEO_GRB` pixel on `PIN_NEOPIXEL`). Measured values are reachable only over
the network. Adding a 200×200 e-paper readout runs into three pre-existing
constraints in this codebase, all of them already documented in the source:

**1. Internal SRAM is the scarce resource, not flash or PSRAM.**
`src/OTAUpdater.h` records that the Arduino ESP32-S2 SDK sets
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so allocations larger than 4 KB go
to PSRAM first, while anything smaller — and everything in BSS, and every
FreeRTOS task stack — comes out of internal SRAM. The measured steady state is
roughly 24 KB internal free. The OTA pre-flight gate (`MIN_FREE_INTERNAL`) is
20480 B and `Network::task()` restarts the device below
`MIN_FREE_INTERNAL_BYTES` = 16384 B (`src/Network.cpp:554`). A change that
consumes 5 KB of internal SRAM does not crash anything, but it does drop
steady-state free below the OTA gate, which means firmware updates stop working
with the confusing message the project already burned a debugging cycle on:
`Insufficient internal heap: free=24072 (need 32768)`.

**2. Config changes reboot the device.** Every mutating settings route saves to
NVS and calls `config.requestRestart(1000)` — `src/routes/SensorRoutes.cpp:109`
and five sites in `src/routes/SettingsRoutes.cpp`. There is no live-reconfigure
path anywhere in the firmware.

**3. Blocking external calls on the Network task have a watchdog contract.**
The `system-architecture` *FreeRTOS task structure* requirement states that a
task body making a blocking external call that may exceed the per-iteration
budget must feed `esp_task_wdt_reset()` immediately before and after it. An
e-paper full refresh blocks ~2 s waiting on BUSY, and a stuck BUSY line blocks
for GxEPD2's internal timeout.

The panel is the Waveshare 1.54" **V2**, controller **SSD1681**, 200×200 mono.
This variant supports partial refresh (~0.3 s, no flash) in addition to full
refresh (~2 s, black/white flash). The tri-colour 1.54" B/C variant has no
partial refresh and a ~15 s full refresh; it is explicitly out of scope.

## Goals / Non-Goals

**Goals:**

- Show current temperature and relative humidity on a 200×200 SSD1681 panel.
- Runtime-configurable via the web UI, **default off**, so existing devices are
  unaffected by a firmware update.
- Cost no more internal SRAM than the project can spare without disturbing the
  OTA gate.
- Keep the refresh *decision* logic host-testable, matching the existing
  `#ifdef ARDUINO` split used throughout (`Sensor`/`I2CSensor`,
  `Config` validators, `support/*`).
- Protect the panel's service life: no unbounded refresh rate, periodic full
  refresh to clear ghosting.
- Fail visibly and safely if the panel is configured but absent.

**Non-Goals:**

- Rendering measurements beyond temperature and humidity. `SensorController`
  exposes CO₂, VOC, pressure, lux and PM2.5, and a generic
  "render the first N measurements" layout is tempting, but it makes the layout
  variable and the partial-refresh window unstable. Deferred until the fixed
  layout has been lived with.
- Supporting other Waveshare panels. The GxEPD2 panel class is a compile-time
  template parameter; adding a second panel means a second instantiation and a
  second page buffer, which is a real memory decision, not a config flag.
- Runtime-configurable GPIO — see D2.
- Deep-sleep integration. E-paper's zero-power image retention makes it an
  obvious future companion to `POWER_OPTIMIZATION.md`, but that document's own
  conclusion is that DFS does not currently work under Arduino + `WIFI_PS_NONE`.
- OTA progress on the panel. Refreshes are suppressed entirely during OTA
  (see D7); showing progress would mean competing for internal SRAM and SPI
  during the exact window the device is most fragile.

## Decisions

### D1. Paged rendering at `HEIGHT / 8` (625 B), not a full framebuffer

```cpp
// src/display/EPaperDisplay.cpp
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT / 8> display(
    GxEPD2_154_D67(DisplayPins::CS, DisplayPins::DC,
                   DisplayPins::RST, DisplayPins::BUSY));
```

`GxEPD2_154_D67` is the SSD1681 / GDEH0154D67 200×200 class. The second template
parameter is the page height in rows; the buffer is
`(WIDTH / 8) * page_height` = `(200 / 8) * 25` = **625 bytes**.

| Page height | Buffer | Pages | Internal free after |
|---|---|---|---|
| 200 (full)  | 5000 B | 1 | ~19.1 KB — **below the 20480 B OTA gate** |
| 100         | 2500 B | 2 | ~21.6 KB |
| 50          | 1250 B | 4 | ~22.8 KB |
| **25**      | **625 B** | **8** | **~23.4 KB** |

**Rationale.** The full buffer is the only option that actually breaks
something, and it breaks OTA — the least visible, most expensive failure mode
available. The cost of paging is that the draw callback runs once per page
inside a `do { … } while (display.nextPage());` loop; for a handful of text
strings that is a few hundred microseconds of extra glyph rasterisation, and
Adafruit GFX clips glyphs that straddle a page boundary correctly. 25 rows is
chosen over 50 because the extra 625 B saved is free and 8 iterations of a text
draw is still trivial.

**Note on "default off" not reclaiming the buffer.** The page buffer is a
non-static member of a file-scope object, so it lands in BSS and is allocated
whether or not the display is enabled. Heap-allocating it on demand would not
help: 625 B is below `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, so it would be
placed in internal heap anyway. The simple thing is also the correct thing —
declare it at file scope and gate only `init()` on the config flag.

**Alternatives considered**

- *Full buffer plus reclaiming SRAM from the Network task stack (20480 B,
  `src/Network.cpp:884`).* The stack's high-water mark is already logged, and
  shrinking it is the documented lever for internal free. But it makes an
  unrelated, riskier change a prerequisite for this one. If a future feature
  genuinely needs the full framebuffer, that trade is available then.
- *Full buffer plus lowering the OTA gate again.* Rejected: the gate was already
  lowered once, from a value that made OTA impossible, to 20480/4096 based on
  measurement. Moving it to accommodate a display would erase that measurement's
  meaning.

### D2a. CS must not reuse the MISO pad, even though the panel is write-only

The tempting move is to put CS on GPIO37: e-paper is write-only, so the MI pad
carries no signal and looks free. **It is not usable for this.**

`SPI.begin()` on ESP32-S2 attaches MISO unconditionally, and there is no way to
opt out through the Arduino API:

```c
// esp32-hal-spi.c:198
void spiAttachMISO(spi_t * spi, int8_t miso) {
    if (miso < 0) {
#if CONFIG_IDF_TARGET_ESP32S2
        if (spi->num == FSPI) { miso = 37; }   // -1 is REWRITTEN, not skipped
        ...
    }
    pinMode(miso, INPUT);                      // <-- clobbers any output mode
    pinMatrixInAttach(miso, SPI_MISO_IDX(spi->num), false);
}
```

`SPIClass::begin(sck, miso, mosi, ss)` passes its arguments straight through
(`SPI.cpp:103-112`), and the "use board defaults" branch only fires when *all
four* arguments are -1. So `SPI.begin(36, -1, 35, -1)` does not leave MISO
unassigned — it resolves to GPIO37 and calls `pinMode(37, INPUT)`.

The failure this produces is order-dependent and nasty. GxEPD2's `init()`
configures the CS pin as an output and also brings up the SPI bus; if the
`SPI.begin()` lands after the `pinMode(CS, OUTPUT)`, the chip select silently
reverts to an input, the panel never sees an assertion, and the display stays
blank. The symptom is indistinguishable from a wiring fault, and it would depend
on the internal ordering of a third-party library across version bumps.

**Decision.** CS goes on **A0 (GPIO18)**, a pin the SPI peripheral never
touches. GPIO37 is left electrically unconnected; `SPI.begin()` claiming it as
an unread input is harmless.

**Alternatives considered**

- *Call `pinMatrixInDetach(37, false, false)` after `SPI.begin()` to release the
  pin.* Works, but it is undocumented-adjacent hackery that depends on running
  after every `SPI.begin()` anywhere in the firmware, to buy back one pad on a
  board that has four to spare.
- *Use `SS` (GPIO42) from the variant file.* GPIO42 is defined in
  `pins_arduino.h` but is not among the QT Py's castellated pads, so it is not
  physically reachable.

### D2. Compile-time pins in `DisplayPins.h`, not config fields

```cpp
// src/display/DisplayPins.h
namespace DisplayPins {
    constexpr int SCK  = 36;  // SCK pad  — driven by the Arduino `SPI` object
    constexpr int MOSI = 35;  // MO pad   — panel DIN
    constexpr int CS   = 18;  // A0
    constexpr int DC   = 8;   // A3
    constexpr int RST  = 9;   // A2
    constexpr int BUSY = 17;  // A1, input, active HIGH
    // GPIO37 (MI pad) is claimed by SPI.begin() as MISO and left unconnected.
}
```

All values are verified against the board variant file
`framework-arduinoespressif32/variants/adafruit_qtpy_esp32s2/pins_arduino.h`:
`MOSI=35`, `SCK=36`, `MISO=37`, `A0=18`, `A1=17`, `A2=9`, `A3=8`, `SDA1=41`,
`SCL1=40`, `PIN_NEOPIXEL=39`, `NEOPIXEL_POWER=38`.

The four control lines occupy the contiguous `A0`–`A3` block, which also makes
the physical wiring easier to check. This leaves `SDA` (7), `SCL` (6), `TX` (5)
and `RX` (16) unclaimed — the primary `Wire` bus stays intact for a future
second I2C device — and does not touch the STEMMA QT bus (GPIO41/40), the
NeoPixel (GPIO39, power GPIO38), or the boot button (GPIO0).

Flash and PSRAM on this part use GPIO27–32 (`soc/esp32s2/spi_pins.h`:
`SPI_IOMUX_PIN_NUM_HD/CS/MOSI/CLK/MISO/WP` = 27/29/32/30/31/28), none of which
are broken out, so nothing in this map can disturb them.

**Footnote on SPI speed.** The quad-mode IOMUX pins for FSPI on ESP32-S2 are
GPIO9–14 (`SPI2_IOMUX_PIN_NUM_*`); 35/36/37 are the *octal* IOMUX set. Driving
SPI on 35/36 therefore routes through the GPIO matrix rather than IOMUX, which
caps the reliable clock below the IOMUX maximum and adds a little propagation
delay. E-paper runs at 4 MHz, so this is irrelevant here — noted only so it is
not rediscovered as a mystery if something faster is ever put on this bus.

**Rationale for keeping them compile-time.** Runtime pin configuration would
need validation against the STEMMA QT pins, the NeoPixel pins, the ESP32-S2
strapping pins and the USB pins — and a bad value drives a pin the firmware
depends on, potentially breaking the very web UI needed to correct it. The
board is fixed (`board = adafruit_qtpy_esp32s2`), so the pins are effectively a
board property, not a user preference. Reconsider only if a second board target
appears.

### D3. `DisplayConfig` as its own struct, mirroring `SyslogConfig`

```cpp
struct DisplayConfig {
    bool     enabled       = false;  // default off
    uint8_t  rotation      = 0;      // 0..3, mounting orientation
    uint16_t interval      = 60;     // minimum seconds between refreshes, 10..3600
};
```

`SyslogConfig` (`host`, `port`, `enabled`; own `PrefsKeys` block; own 67-line
`SyslogRoutes.cpp`) is the closest existing analogue, and this follows it
exactly rather than swelling the already-crowded `DeviceConfig`.

NVS keys, all within the ≤12-character reliability rule that `PrefsKeys.h`
documents:

```
disp_enabled   12 chars
disp_rot        8
disp_intv       9    <- NOT "disp_interval" (13 chars, exceeds the limit)
```

`disp_interval` at 13 characters is exactly the trap that produced the existing
`wifi_sleep` abbreviation; the field is named `interval` in C++ and stored under
`disp_intv`.

`validateDisplayConfig()` clamps `rotation` to 0..3 and `interval` to
10..3600 s, and lives next to the other validators in `Config.cpp` so the
existing native `test_config` suite can cover it.

### D4. Refresh policy: hysteresis, an interval floor, and periodic full refresh

```
                 new snapshot (temperature, humidity, valid, nowMs)
                                    │
                                    ▼
                    ┌───────────────────────────────┐
              no    │ first evaluation since boot?  │
        ┌───────────┤                               │
        │           └───────────────┬───────────────┘
        ▼                           │ yes
  ┌──────────────────────────┐      └──────────────► Full
  │ |Δtemp| >= 0.2 °C  OR    │
  │ |Δrh|   >= 1.0 %RH  OR   │──no──► None
  │ validity flipped         │
  └────────────┬─────────────┘
               │ yes
               ▼
  ┌──────────────────────────┐
  │ nowMs - lastRefresh      │──no──► None   (deferred; re-evaluated next tick)
  │   >= interval * 1000     │
  └────────────┬─────────────┘
               │ yes
               ▼
  ┌──────────────────────────┐
  │ partialsSinceFull >= 12  │──yes─► Full   (resets the counter)
  └────────────┬─────────────┘
               │ no
               ▼
             Partial
```

Constants: `TEMP_HYSTERESIS_C = 0.1f`, `HUMIDITY_HYSTERESIS_PCT = 1.0f`,
`FULL_REFRESH_EVERY_N_PARTIALS = 12`, minimum interval from
`DisplayConfig::interval` (default 60 s).

**Rationale.** The Sensor Monitor task reads at 1 Hz. Refreshing an e-paper
panel at 1 Hz would destroy it — Waveshare's own guidance is not to refresh more
often than roughly every 180 s for longevity, and partial refreshes accumulate
ghosting until a full refresh clears it. Three independent brakes are needed
because each catches a different failure: hysteresis suppresses sensor noise
dithering the last digit, the interval floor bounds the worst case when a value
is genuinely sweeping, and the partial counter bounds ghosting. With the
defaults, a value changing continuously produces at most one refresh per minute
and a flashing full refresh at most every 12 minutes.

`NaN` from `getTemperature()`/`getRelativeHumidity()` (the documented
"not available" return) is treated as invalid, and a validity transition in
either direction forces a refresh so the panel switches between the reading and
the `--.-` placeholder promptly.

`RefreshPolicy` takes `nowMs` as a parameter rather than calling `millis()`, so
the native test can drive time directly. `millis()` rollover at ~49.7 days is
handled by unsigned subtraction, the same idiom the Network and Sensor tasks
already use.

**Alternatives considered**

- *Refresh on a fixed timer, ignoring value changes.* Simpler, but wastes panel
  life redrawing an unchanged number, and the ghosting counter would advance for
  no reason.
- *Refresh on every change with only an interval floor.* Drops the hysteresis
  brake, so a sensor oscillating between 21.44 °C and 21.46 °C refreshes every
  minute forever with no visible difference on a one-decimal display.

### D5. Clear-on-disable inline in the request handler

E-paper retains its image with no power. Disabling the display without blanking
it leaves the panel showing a frozen, increasingly stale reading.

The `POST /api/display` handler therefore blanks the panel synchronously on the
disable transition, while it is still initialised, before saving and scheduling
the restart. No state is persisted to defer the work, and boot has no blanking
path at all.

**Superseded design.** An earlier revision persisted a `disp_clear` one-shot and
blanked on the next boot, to keep a ~2.6 s refresh out of the AsyncTCP callback
and to stay correct if power dropped between the save and the clear. That was
reversed deliberately: it added a fourth NVS key and a three-way boot branch to
protect against a power cut in a one-second window during a deliberate user
action, and the recovery from losing that race is simply toggling the setting
again. The inline version is materially simpler for a negligible loss.

**What the reversal does require.** The handler runs on the AsyncTCP task while
`DisplayManager::update()` runs on the Network task. Clearing inline puts two
tasks on the same GxEPD2 object and SPI bus, so `DisplayManager` now owns a
mutex:

```
AsyncTCP task                     Network task
─────────────                     ────────────
disableAndClear()                 update()
  enabled = false  ──────────────►  (early-out on !enabled)
  take(portMAX_DELAY) ····waits···  [holding lock, mid-repaint]
  panel.clear()                     give()
  give()
```

`update()` holds the lock across its whole body rather than only the repaint:
otherwise it could pass the `enabled` check, block on the lock while the handler
clears, then acquire it and immediately paint over the blanked panel. It uses a
100 ms timeout — if the handler holds the lock we are being disabled anyway —
while `disableAndClear()` waits indefinitely, since the holder is a bounded
panel operation already capped by the fault guard (D8).

### D6. Layout and the partial-refresh window

```
      x=0                                        x=199
 y=0  ┌────────────────────────────────────────┐
      │                                        │
      │                                        │
 y=30 │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ ┐
      │            21.4°                       │ │
      │        FreeSansBold24pt7b              │ │  partial-refresh
      │                                        │ │  window:
      │            47 %rH                      │ │  (0, 30) 200 x 160
      │         FreeSans12pt7b                 │ │
      │  ────────────────────────────────      │ │
      │  klima-a1b2                 14:32      │ │  footer INSIDE the window
 y=190│ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ ┘
 y=199└────────────────────────────────────────┘
```

The partial window spans **both** the value block and the footer,
`(0, 30) 200×160`.

An earlier revision confined it to the value block and repainted the footer only
on full refreshes. Hardware testing exposed that as wrong: with a stable sensor
there may be no refresh for hours, so the clock simply froze. The mistake was
conceptual rather than geometric — treating that field as a *wall clock* on a
display which, by design, only repaints when a value changes.

Reframed, it is the **timestamp of the reading above it**: "this 21.4 °C was
measured at 14:32". Under that reading it plainly belongs inside the refresh
window, and its not advancing between refreshes becomes correct behaviour rather
than staleness — it tells the viewer how fresh the number is, which is genuinely
useful on a panel that updates irregularly.

The rejected alternative is scheduling a refresh whenever the minute changes.
That produces 60 refreshes an hour, at or below the configured minimum interval,
defeating the hysteresis and interval brakes D4 exists to enforce and spending
panel life to animate a clock the user's phone already shows.

Placeholder rendering when data is invalid: `--.-°` and `-- %rH`.

`rotation` is applied once in `begin()` via `display.setRotation()`; the layout
coordinates are rotation-independent because GFX transforms them.

### D7. The refresh runs on the Network task, suppressed during OTA

`DisplayManager::update()` is called once per second from `Network::task()`'s
1-second loop, immediately after `statusLed.update()` — where a
`touchController->update()` call is already stubbed out in a comment
(`src/Network.cpp:574`), showing that peripheral updates were always intended to
hang off this loop.

```cpp
statusLed.update();

if (display && !otaActive) {
    display->update();      // internally a no-op unless RefreshKind != None
}
```

`update()` returns immediately in the common case; it only blocks when the
policy actually calls for a refresh, i.e. at most once per `interval` seconds.

**Watchdog contract.** When a refresh does happen, `EPaperDisplay` feeds
`esp_task_wdt_reset()` immediately before and after the blocking
`nextPage()` loop, as the *FreeRTOS task structure* requirement demands for
blocking external calls. A ~2 s full refresh is well inside the 30 s TWDT
budget, but a stuck BUSY line is not necessarily.

**Suppression during OTA.** `otaActive` is already sampled once per loop
iteration and used to stand down every subsystem that competes for internal
SRAM or misreads failures as link loss. A display refresh during an OTA download
would contend for SPI and internal memory during the most fragile window the
device has.

**Alternatives considered**

- *A dedicated FreeRTOS display task.* The clean answer to "a wedged BUSY line
  should not stall MQTT". Rejected because a task stack is at minimum ~4 KB of
  internal SRAM — 6× the entire page buffer — which is precisely the resource
  D1 went to trouble to protect. The BUSY fault guard in D8 addresses the same
  risk for a fraction of the cost.
- *Folding it into the Sensor Monitor task.* That task's 1-second cadence drives
  sensor reads and the PID controller; a 2 s block would skew it.

### D8. BUSY fault guard: three strikes, in-memory, non-persistent

Each refresh is wall-clock timed. A refresh exceeding
`REFRESH_TIMEOUT_MS = 12000` counts as a timeout; `MAX_CONSECUTIVE_TIMEOUTS = 3`
puts `EPaperDisplay` into a `faulted` state that makes every subsequent
`render()` a no-op, logged once at `ESP_LOGE`. Any successful refresh resets the
counter.

**Rationale.** If the display is enabled in config but the panel is unplugged or
the BUSY line is stuck, GxEPD2 blocks on its internal busy timeout on every
attempt, once per `interval`, on the Network task. Three strikes distinguishes a
genuinely absent panel from a transient glitch, and 12 s is comfortably above a
normal ~2 s full refresh while staying inside the 30 s TWDT.

**Rationale for not persisting the fault.** Writing `enabled = false` to NVS on
a hardware fault would mean a loose connector silently rewrites the user's
configuration, and the web UI would then show the display as disabled when the
user never disabled it. The fault is a runtime condition; a reboot re-tests the
hardware, which is the correct behaviour for a reseated cable.

### D9. Testability split

```
   RefreshPolicy                       ← no ARDUINO deps, native-testable
   ├─ evaluate(temp, rh, valid, nowMs) → RefreshKind
   ├─ hysteresis / interval floor / ghosting counter
   └─ formatTemperature() / formatHumidity()  (incl. NaN → "--.-")
            │
            ▼
   EPaperDisplay                       ← #ifdef ARDUINO
   ├─ GxEPD2, SPI, GFX, the paged draw loop
   └─ BUSY timing and the fault guard
            │
            ▼
   DisplayManager                      ← #ifdef ARDUINO
   └─ owns both; holds SensorController&; update() called from Network
```

`+<display/RefreshPolicy.cpp>` and `+<test/test_display_refresh_policy/>` join
the native `build_src_filter`. This mirrors the split the project already uses
for `Sensor`/`I2CSensor`, the `Config` validators and everything under
`src/support/`.

The refresh state machine is exactly the class of logic that is miserable to
debug on hardware — "why did it flash twelve times in a row" — and trivial to
assert on the host.

### D10. A standalone wiring document, not just comments in `DisplayPins.h`

This is the project's first external peripheral that has to be physically wired;
every sensor so far arrives on a STEMMA QT cable that cannot be connected wrong.
The wiring therefore needs a document someone can follow with the board in one
hand and the module in the other, without reading firmware.

`docs/EINK_DISPLAY_WIRING.md`, linked from `README.md`, alongside the existing
`docs/MDNS.md` and `docs/OTA_*.md`:

1. **Which product to buy** — the "1.54inch e-Paper Module" with the 8-pin
   2.54 mm header, and how to tell it apart from the tri-colour (B/C) and the
   bare-panel-plus-Driver-HAT packagings, only one of which this firmware
   supports.
2. **The wiring table** — panel signal → GPIO → QT Py pad silkscreen name →
   Waveshare ribbon colour. Named by silkscreen label rather than physical
   position, since pad ordering is not something the firmware can assert.
3. **Why GPIO37 is not used**, in one line with a pointer to D2a — otherwise the
   next person looks at the spare MI pad and "fixes" the layout.
4. **Power and decoupling** — 3V3 only, and the 100 µF bulk cap rationale tied
   to the brownout history in `main.cpp`.
5. **Pre-power-on checklist** — continuity and short checks before applying
   power, since a reversed VCC/GND on a 3.3 V panel is destructive.
6. **Troubleshooting by symptom** — blank panel, garbage, permanent BUSY, ghost
   images, brownout resets — each mapped to a likely cause, and cross-referenced
   to the firmware's `ESP_LOGE` lines and the fault guard in D8.

**Rationale for a separate file over expanded header comments.** `DisplayPins.h`
is read by people editing firmware; this is read by people holding a soldering
iron. The audiences barely overlap, and the pinout table is the one artefact
that must be correct before any code runs. Keeping it in `docs/` also matches
where the project already puts operational guides.

The firmware remains the source of truth for pin numbers: the document restates
`DisplayPins.h` and the two must be changed together. A task in this change
records that coupling.

## Risks / Trade-offs

- **Risk: the 625 B is spent even when the display is disabled.** → Accepted and
  documented in D1. Reclaiming it would require conditional compilation, which
  contradicts the runtime-configurable requirement.
- **Risk: flash growth (60–80 KB) from GxEPD2 plus two GFX fonts.** → Current
  firmware is 1,270 KB against a 1,856 KB app slot and a 1900 KB spec cap; the
  margin is ~586 KB. The build-size scenario in `system-architecture` remains
  satisfied. If fonts prove expensive, the 12pt font can be dropped in favour of
  a scaled default font.
- **Risk: a display refresh stalls MQTT publishing by ~2 s.** → The MQTT publish
  interval defaults to 15 s and the display refreshes at most once per 60 s, so
  at worst one publish per minute is delayed by ~2 s. The MQTT payload carries
  its own NTP timestamp, so the delay does not corrupt the data.
- **Risk: an inrush from the e-paper charge pump coinciding with a WiFi TX burst
  triggers a brownout.** `src/main.cpp` already treats `ESP_RST_BROWNOUT` as a
  live suspicion on this board. A refresh peaks around 25 mA for ~2 s. →
  Mitigation is hardware, not firmware: a 100 µF bulk capacitor across the
  module's 3V3/GND. This is documented in the wiring requirement so it is not
  discovered the hard way. `resetReasonStr()` already reports brownouts at boot,
  so the failure mode is observable if it occurs.
- **Risk: the module ships on a Waveshare "e-Paper Driver HAT" with 3.3/5 V and
  4-wire/3-wire SPI jumpers set wrongly.** → Documented in the wiring
  requirement; 3.3 V and 4-wire SPI are required.
- **Risk: a user sets `interval` to 10 s and shortens panel life.** → 10 s is the
  validated floor, below Waveshare's ~180 s longevity guidance but a deliberate
  allowance for people who want a responsive readout and accept the trade. The
  hysteresis brake means 10 s only produces 10 s refreshes if the value is
  genuinely changing that fast.
- **Trade-off: temperature and humidity only, while the data model is generic.**
  `SensorController` moved to generic `Measurement` structs precisely so
  consumers would not hardcode two fields. The fixed layout does exactly that. →
  Accepted for v1: the fixed layout is what makes the partial-refresh window
  stable, and `RefreshPolicy` is deliberately shaped around two float channels
  so widening it later is a contained change.

## Migration Plan

No data migration. `loadDisplayConfig()` returns the defaults
(`enabled = false`) for any device whose NVS has never held the new keys, so
existing devices are unaffected until an operator opts in.

1. Add `DisplayConfig`, the NVS keys, the validator, and load/save in
   `Config.{h,cpp}` + `PrefsKeys.h`.
2. Add `RefreshPolicy` and its native test suite; wire both into
   `platformio.ini`'s native `build_src_filter`. Verify `pio test -e native`
   before any hardware code exists.
3. Add the `GxEPD2` dependency, `DisplayPins.h`, `EPaperDisplay`, and
   `DisplayManager`.
4. Wire into `main.cpp` (file-scope object, config-gated `begin()`, splash,
   clear-on-disable) and `Network` (`setDisplay()` + the loop call).
5. Add `DisplayRoutes.cpp` and register it in `WebServerManager`.
6. Add the Display tab to `data/settings.html`.
7. Build (`pio run -e adafruit_qtpy_esp32s2`), record the flash delta, and
   confirm it stays under the 1900 KB cap.
8. Run `pio test -e native`.
9. Hardware verification: wire the panel per D2, enable via the web UI, confirm
   the splash, a first full refresh, subsequent partials, a full refresh on the
   12th partial, and clear-on-disable. Confirm `/api/status` still reports
   internal free above 20480 B so OTA remains available. Confirm an OTA update
   still succeeds with the display enabled.
10. Unplug the panel with the display enabled and confirm the fault guard trips
    after three attempts without a watchdog reset.

**Rollback.** Revert the source, `platformio.ini` and spec commits. The NVS keys
left behind are inert — an older firmware never reads them, and
`factory-reset` clears the whole `klima` namespace. No existing endpoint,
struct or on-wire format is altered, so a rollback needs no coordination.

## Open Questions

None blocking. Two items deliberately deferred rather than unresolved:

- Whether the footer clock is worth its full-refresh-only staleness, or should
  be dropped entirely. Resolvable only by looking at the mounted device.
- Whether `rotation` and `interval` earn their place in the UI, or whether
  `enabled` alone would have been enough. Both are cheap given the route exists,
  and removing a field later is easier than discovering the panel is mounted
  upside down with no way to fix it.
