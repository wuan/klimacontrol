# display Specification

## Purpose

Allow visualizing basic sensor data like temperature and humidity from the sensor board.

Possible future expansion to include more sensors and more complex visualizations.
## Requirements
### Requirement: Hardware target and wiring

The firmware SHALL support a single [Waveshare 1.54" V2 e-paper module](https://www.waveshare.com/wiki/1.54inch_e-Paper_Module_Manual) (SSD1681
controller, 200×200 pixels, monochrome) connected over the board's hardware SPI
bus. The panel SHALL be driven via the GxEPD2 panel class `GxEPD2_154_D67`.

Pin assignments SHALL be compile-time constants in `src/display/DisplayPins.h`
and SHALL NOT be runtime-configurable:

| Panel signal | GPIO | QT Py ESP32-S2 pad |
|---|---|---|
| `CLK`  | 36 | `SCK` (Arduino `SPI`) |
| `DIN`  | 35 | `MO` (Arduino `SPI`) |
| `CS`   | 18 | `A0` |
| `DC`   | 8  | `A3` |
| `RST`  | 9  | `A2` |
| `BUSY` | 17 | `A1`, input, active HIGH |
| `VCC`  | —  | `3V` |
| `GND`  | —  | `GND` |

The `MI` pad (GPIO37) SHALL be left unconnected.

The assignment SHALL NOT use the STEMMA QT I2C pins (`SDA1`=GPIO41,
`SCL1`=GPIO40), the NeoPixel pins (GPIO39 data, GPIO38 power), the boot button
(GPIO0), or any pin in the flash/PSRAM range GPIO27–32.

The chip-select line SHALL NOT be assigned to GPIO37. Although the panel is
write-only and the `MI` pad carries no signal, `SPI.begin()` on this target
attaches MISO unconditionally — passing `-1` resolves to GPIO37 and executes
`pinMode(37, INPUT)` — which would silently reconfigure a chip-select placed
there and leave the panel unresponsive.

The module SHALL be powered from the 3.3 V rail. Where the module ships on a
Waveshare e-Paper Driver HAT carrier, the carrier's voltage jumper SHALL be set
to 3.3 V and its SPI jumper to 4-wire mode.

#### Scenario: SPI bus does not contend with the sensor bus

- **WHEN** the display is enabled and sensors are attached to the STEMMA QT connector
- **THEN** display SPI traffic and sensor I2C traffic SHALL use disjoint pins, and no I2C bus lock SHALL be required for a display refresh

#### Scenario: Panel is write-only

- **WHEN** the display is initialised
- **THEN** the firmware SHALL NOT read from the panel, and the `MI` pad SHALL carry no panel signal

#### Scenario: Chip select survives SPI bus initialisation

- **WHEN** the SPI bus is initialised and the display's chip-select pin is configured as an output, in either order
- **THEN** the chip-select pin SHALL remain an output, and SHALL therefore not be GPIO37

#### Scenario: Pin map matches the board variant

- **WHEN** the pin constants in `src/display/DisplayPins.h` are reviewed against `variants/adafruit_qtpy_esp32s2/pins_arduino.h`
- **THEN** `SCK`, `MOSI`, `A0`, `A1`, `A2` and `A3` SHALL resolve to GPIO 36, 35, 18, 17, 9 and 8 respectively

### Requirement: Wiring documentation

The repository SHALL contain `docs/EINK_DISPLAY_WIRING.md`, linked from
`README.md`, giving the hardware-side instructions needed to connect the panel
without reading firmware source.

It SHALL cover: which Waveshare product variant is supported and how to identify
it; the complete signal → GPIO → pad → ribbon-colour wiring table; the reason
GPIO37 is not used for chip select; power and decoupling guidance; a
pre-power-on continuity checklist; and a symptom-to-cause troubleshooting table.

The wiring table SHALL identify pads by their silkscreen label rather than
physical position, and SHALL stay consistent with `src/display/DisplayPins.h`.

#### Scenario: Wiring the panel from documentation alone

- **WHEN** an operator has the board and the module and follows `docs/EINK_DISPLAY_WIRING.md`
- **THEN** they SHALL be able to complete the wiring, verify it before applying power, and identify the cause of a blank or garbled panel without reading the firmware

#### Scenario: Documentation and firmware pin map agree

- **WHEN** a pin constant in `src/display/DisplayPins.h` is changed
- **THEN** the wiring table in `docs/EINK_DISPLAY_WIRING.md` SHALL be updated in the same change

### Requirement: Paged rendering with a bounded page buffer

The firmware SHALL render the panel using GxEPD2's paged mode with a page height
of `GxEPD2_154_D67::HEIGHT / 8` (25 rows), giving a page buffer of
`(200 / 8) * 25` = 625 bytes. The firmware SHALL NOT allocate a full-screen
200×200 framebuffer.

The page buffer resides in BSS (internal SRAM) and is therefore allocated
regardless of whether the display is enabled.

#### Scenario: Page buffer size

- **WHEN** the firmware is built for `adafruit_qtpy_esp32s2`
- **THEN** the GxEPD2 display object SHALL be instantiated as `GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT / 8>`, consuming 625 bytes of internal SRAM

#### Scenario: Display does not disturb the OTA heap gate

- **WHEN** the firmware is running normally with the display enabled and no OTA in progress
- **THEN** free internal heap SHALL remain above the OTA pre-flight gate of 20480 bytes, so firmware updates remain possible

#### Scenario: Drawing spans multiple pages

- **WHEN** a refresh is performed
- **THEN** the draw callback SHALL be invoked once per page inside a `do { … } while (display.nextPage());` loop, and glyphs straddling a page boundary SHALL render correctly

### Requirement: Runtime configuration, default off

The display SHALL be configured at runtime via `Config::DisplayConfig` and SHALL
default to disabled, so a device flashed with this firmware and not reconfigured
behaves as it did before apart from the BSS and flash cost.

Configuration changes SHALL be persisted to NVS and SHALL take effect via a
device restart, matching the save-then-`requestRestart()` convention used by
every other settings route.

When the display is disabled, the firmware SHALL NOT call `display.init()` and
SHALL NOT claim the SPI or control pins.

#### Scenario: Default state on a device that has never been configured

- **WHEN** a device boots with no display keys present in NVS
- **THEN** `loadDisplayConfig()` SHALL return `enabled = false`, `rotation = 0`, `interval = 60`, and the panel SHALL never be initialised

#### Scenario: Enabling the display

- **WHEN** an operator enables the display via the settings UI
- **THEN** the configuration SHALL be persisted, a restart SHALL be scheduled, and after the restart the panel SHALL be initialised and painted

### Requirement: Displayed content

The display SHALL show the current temperature and the current relative
humidity, sourced from `SensorController` under a single consistent snapshot.
No other measurement type SHALL be rendered.

The layout SHALL place the temperature as the primary value and the humidity
below it, with the two-line footer described in *Two-line footer layout*. The
value block SHALL occupy the region `(0, 30)` to `(199, 139)`; the footer SHALL
occupy the region below it, from the rule at y=152 to the bottom of the panel.

When a value is unavailable — the sensor snapshot is invalid, or the accessor
returns `NAN` — the firmware SHALL render a placeholder (`--.-` for temperature,
`--` for humidity, `--` for the setpoint) rather than a stale or zero value.

#### Scenario: Both values available

- **WHEN** the sensor snapshot is valid and reports 21.4 °C and 47 %RH
- **THEN** the panel SHALL show the temperature to one decimal place and the humidity as a whole number, with the temperature rendered in the larger font

#### Scenario: No sensor attached

- **WHEN** the display is enabled but no sensor is configured, so `getTemperature()` returns `NAN`
- **THEN** the panel SHALL render the placeholder text rather than a numeric value

#### Scenario: Values are read atomically

- **WHEN** the display gathers the values to render
- **THEN** it SHALL use `SensorController::getSnapshot()` (or an equivalent single-lock accessor) so temperature, humidity and the validity flag describe the same instant

#### Scenario: Setpoint unavailable

- **WHEN** the target temperature is `NAN`
- **THEN** the setpoint field SHALL render `--` rather than a number

### Requirement: Two-line footer layout

The e-paper footer SHALL occupy two text lines in two columns, separated from the
value block by a horizontal rule:

```
--------------------------------------------  <- rule, y = 152
klimacontrol                       22.0 (o)   <- line 1, baseline y = 170
2026-09-01 12:34                        (*)   <- line 2, baseline y = 192
```

The left column SHALL carry the device name on line 1 and the local date and time
on line 2, both flush left at x=6. The right column SHALL carry the setpoint with
its degree mark on line 1 and the control state symbol on line 2, both flush
right at x=194. Every field SHALL use the FreeSans9pt7b font.

Anchoring each column to a fixed margin rather than centring it means neither
column shifts as its content changes width.

#### Scenario: Device name and date/time occupy the left column

- **WHEN** the footer is rendered and NTP has synced
- **THEN** the device name SHALL appear on line 1 and the local date and time SHALL appear on line 2, both left aligned

#### Scenario: Clock unsynced leaves the date/time blank

- **WHEN** `getCurrentEpoch()` returns 0 because NTP has not synced
- **THEN** line 2 of the left column SHALL be empty rather than showing a date derived from the Unix epoch

#### Scenario: Long device name is truncated, not overrun

- **WHEN** the device name is wider than the space the right column leaves free
- **THEN** the name SHALL be truncated with a trailing `.` so that it cannot be drawn over the setpoint or the control symbol

#### Scenario: Left column is measured per line

- **WHEN** the footer is rendered
- **THEN** each left-column field SHALL be truncated against the width its own row leaves free, so line 2 may be wider than line 1

### Requirement: Control state symbol on e-paper display

The e-paper display SHALL show the temperature control state using drawn symbols
in the right column of the footer, on line 2, directly below the setpoint. The
symbol SHALL be drawn using GFX primitives: a 10px horizontal line for
**Inactive**, a 12px diameter hollow circle for **Active Off**, or a 12px
diameter filled circle for **Active On**.

#### Scenario: Inactive state shows minus line

- **WHEN** control is disabled
- **THEN** the footer SHALL display a 10px wide horizontal line

#### Scenario: Active Off state shows hollow circle

- **WHEN** control is enabled and output is zero
- **THEN** the footer SHALL display a hollow circle with 6px radius (12px diameter)

#### Scenario: Active On state shows filled circle

- **WHEN** control is enabled and output is non-zero
- **THEN** the footer SHALL display a filled circle with 6px radius (12px diameter)

#### Scenario: Setpoint displayed above the symbol

- **WHEN** the display is rendering
- **THEN** the setpoint value SHALL be right aligned on footer line 1, with the control symbol right aligned directly below it on line 2

#### Scenario: Degree symbol drawn as circle

- **WHEN** the setpoint is displayed
- **THEN** a small circle with 2px radius SHALL be drawn immediately to the right of the setpoint text, at the digits' cap height, to indicate degrees Celsius

### Requirement: Refresh policy

The firmware SHALL decide between no refresh, a partial refresh, and a full
refresh using a pure decision function that takes the temperature, the humidity,
a validity flag and the current millisecond clock, and returns one of
`RefreshKind::{None, Partial, Full}`.

The policy SHALL apply, in order:

1. **First paint** — the first evaluation after boot SHALL return `Full`.
2. **Change detection** — a refresh SHALL be considered when any of the
   following differs from what is currently rendered: the temperature has moved
   at least 0.1 °C; the humidity has moved at least 1.0 %RH; the validity flag
   has changed in either direction; or the wall-clock minute has rolled over.
3. **Minimum interval** — a refresh SHALL NOT occur within
   `DisplayConfig::interval` seconds of the previous refresh. A change
   suppressed by this floor SHALL be re-evaluated on subsequent ticks rather
   than discarded.
4. **Ghosting** — every 12th consecutive partial refresh SHALL be promoted to a
   full refresh, resetting the counter.

Elapsed-time comparisons SHALL use unsigned subtraction so the policy behaves
correctly across the `millis()` rollover.

The decision function SHALL be free of Arduino and FreeRTOS dependencies and
SHALL be compiled and unit-tested in the `native` PlatformIO environment.

#### Scenario: Noise below the hysteresis threshold

- **WHEN** the temperature moves by less than 0.1 °C, the humidity is unchanged, and the clock minute is unchanged
- **THEN** the policy SHALL return `None` and the panel SHALL NOT be refreshed

#### Scenario: The clock advances on its own

- **WHEN** the wall-clock minute rolls over and no measured value has changed
- **THEN** the policy SHALL return a refresh, so the footer shows a live clock rather than the timestamp of the last reading

#### Scenario: The clock cannot outrun the refresh budget

- **WHEN** the configured minimum interval is longer than 60 seconds
- **THEN** clock-driven refreshes SHALL still be subject to that floor, so the displayed time may lag by up to one interval

#### Scenario: Clock-driven refreshes count toward the ghosting budget

- **WHEN** a refresh is triggered solely by the clock advancing
- **THEN** it SHALL advance the partial counter exactly as a value-driven refresh does, so the periodic full refresh still clears ghosting

#### Scenario: Change inside the minimum interval

- **WHEN** the temperature has moved 0.5 °C but only 20 seconds have elapsed since the last refresh and `interval` is 60
- **THEN** the policy SHALL return `None`, and once 60 seconds have elapsed the still-outstanding change SHALL produce a refresh

#### Scenario: Periodic full refresh clears ghosting

- **WHEN** 12 partial refreshes have occurred since the last full refresh and a further refresh is due
- **THEN** the policy SHALL return `Full` and reset the partial counter

#### Scenario: Validity transition forces a refresh

- **WHEN** the sensor snapshot transitions from valid to invalid, or from invalid to valid
- **THEN** the change SHALL bypass the value hysteresis check so the panel switches between the reading and the placeholder promptly

#### Scenario: Clock rollover

- **WHEN** `millis()` wraps from near `UINT32_MAX` to a small value between two evaluations
- **THEN** the minimum-interval check SHALL still compute a correct elapsed time and SHALL NOT suppress refreshes for the following 49 days

### Requirement: Setpoint and control state drive the refresh decision

The refresh policy SHALL treat a change of the displayed setpoint or of the
control state as a change worth showing, subject to the same minimum-interval
floor as every other refresh.

Without this, the two fields would only reach the panel when a measured value
happened to cross its hysteresis band or the wall-clock minute rolled over — and
the minute never rolls over while NTP is unsynced, so a stable sensor could hold
a stale setpoint indefinitely.

#### Scenario: Retargeting repaints the panel

- **WHEN** the user changes the target temperature and no measured value has changed
- **THEN** the policy SHALL return a refresh once the minimum interval has passed

#### Scenario: Control state transition repaints the panel

- **WHEN** the control state changes between Inactive, Active Off and Active On
- **THEN** the policy SHALL return a refresh once the minimum interval has passed

#### Scenario: Sub-precision setpoint movement is suppressed

- **WHEN** the setpoint changes by less than half of the rendered precision (0.05 K)
- **THEN** the policy SHALL NOT refresh on that account, because the rendered text is unchanged

### Requirement: Partial refresh window

A `Partial` refresh SHALL update the region `(0, 30)` 200×170 via
`setPartialWindow()`, i.e. down to the bottom edge of the panel. This window
SHALL contain every element that can change between refreshes: both value lines
and both footer lines.

The footer's date/time field SHALL be a **live wall clock**. It SHALL be redrawn
on every refresh, partial included, and the minute rolling over SHALL itself
trigger a refresh (see *Refresh policy*), so the displayed time tracks real time
rather than only the moment of the last reading.

The window was widened from 160 to 170 rows when the footer became two lines
tall: a field left outside it would freeze between full refreshes, which on a
stable sensor could be indefinitely.

#### Scenario: Partial refresh does not flash

- **WHEN** the policy returns `Partial`
- **THEN** the region SHALL be rewritten without the black/white inversion flash of a full refresh

#### Scenario: Clock stays in step with the reading

- **WHEN** a partial refresh repaints the temperature and humidity
- **THEN** both footer lines SHALL be repainted in the same operation

#### Scenario: A stable sensor still advances the clock

- **WHEN** the measured values remain inside the hysteresis band for an extended period
- **THEN** the panel SHALL still refresh as the minute rolls over, subject to the configured minimum interval

### Requirement: Boot splash

When the display is enabled, the firmware SHALL paint a splash screen at the end
of `setup()` showing the device name and an indication that the device is
starting, before any sensor reading is available.

#### Scenario: Booting device is visibly distinct from a dead one

- **WHEN** a device with the display enabled powers on
- **THEN** the panel SHALL show the device name and a "starting" indication within the boot sequence, and SHALL be replaced by the first measurement once a valid sensor snapshot exists

### Requirement: Clear on disable

Because e-paper retains its image without power, disabling the display SHALL
result in the panel being blanked to white before the device restarts.

The blanking SHALL be performed synchronously in the `POST /api/display`
handler, on the disable transition, while the panel is still initialised. The
firmware SHALL NOT persist a deferred "clear pending" flag, and SHALL NOT blank
the panel during boot.

Because the handler runs on the AsyncTCP task while the periodic refresh runs on
the Network task, panel access SHALL be serialised by a mutex owned by the
display manager, so the two cannot drive GxEPD2 and the SPI bus concurrently.
Once disabled, the manager SHALL NOT repaint even if a refresh was already
pending.

#### Scenario: Disabling the display blanks the panel

- **WHEN** an operator disables the display via the settings UI
- **THEN** the panel SHALL be blanked to white within the request handler, the configuration SHALL be saved, and a restart SHALL be scheduled

#### Scenario: Concurrent refresh does not corrupt the clear

- **WHEN** the disable request arrives while the Network task is mid-repaint
- **THEN** the handler SHALL wait for that repaint to complete before clearing, and the Network task SHALL NOT repaint afterwards

#### Scenario: No deferred state is persisted

- **WHEN** the display is disabled
- **THEN** no "clear pending" key SHALL be written to NVS, and the next boot SHALL NOT initialise the panel

### Requirement: Refresh runs on the Network task and is suppressed during OTA

The display refresh SHALL be driven from the Network task's one-second loop,
alongside `StatusLed::update()`. It SHALL be skipped entirely while
`OTAUpdater::isUpdateInProgress()` is true.

Because a refresh is a blocking external call that can exceed the per-iteration
budget, the firmware SHALL call `esp_task_wdt_reset()` immediately before and
immediately after the blocking page loop, per the `system-architecture`
*FreeRTOS task structure* requirement.

The display SHALL NOT be given its own FreeRTOS task.

#### Scenario: No refresh during an OTA download

- **WHEN** an OTA update is in progress and a value change would otherwise trigger a refresh
- **THEN** the refresh SHALL be skipped, and the outstanding change SHALL be rendered after the OTA flow completes

#### Scenario: Watchdog is fed around a blocking refresh

- **WHEN** a full refresh blocks for approximately two seconds waiting on the BUSY line
- **THEN** `esp_task_wdt_reset()` SHALL have been called immediately before the blocking loop and SHALL be called immediately after it, so the 30-second task watchdog is not starved

#### Scenario: Common path does not block

- **WHEN** the refresh policy returns `None`
- **THEN** the per-second display update SHALL return without touching the SPI bus

### Requirement: BUSY fault guard

The firmware SHALL time each refresh. A refresh exceeding 12000 ms SHALL count
as a timeout. After 3 consecutive timeouts the display SHALL enter an in-memory
`faulted` state in which further refresh attempts are skipped until the device
restarts, and SHALL log the condition once at `ESP_LOGE`. Any successful refresh
SHALL reset the consecutive-timeout counter.

The firmware SHALL NOT modify the persisted `DisplayConfig` in response to a
hardware fault.

#### Scenario: Panel is configured but not connected

- **WHEN** the display is enabled but the panel is absent, so the BUSY line never releases
- **THEN** after three consecutive timed-out refresh attempts the firmware SHALL log an error, stop attempting refreshes, and continue running WiFi, MQTT and sensor reads normally

#### Scenario: Fault does not rewrite configuration

- **WHEN** the display has entered the faulted state
- **THEN** `GET /api/display` SHALL still report `enabled = true`, and NVS SHALL be unchanged

#### Scenario: Reseated panel recovers on reboot

- **WHEN** a faulted device is restarted with the panel reconnected
- **THEN** the display SHALL initialise normally and the fault state SHALL be cleared

### Requirement: Controller demand bar

The panel footer SHALL show the controller's demand as a bar of discrete segments on footer line 2, positioned between the date/time and the control symbol. The bar SHALL be drawn only while temperature control is enabled: when control is off the inactive symbol already conveys the state, and when control is on the bar SHALL be drawn even at zero demand, so that "enabled and calling for nothing" is distinguishable from "switched off".

A numeric percentage SHALL NOT be shown on the panel. The panel repaints whenever a displayed value changes, so a value that moves on every control tick would hold refreshes at the minimum-interval floor indefinitely, for a precision that is not meaningful on a plant with hours-long dynamics.

The bar SHALL be sized so that the date/time in the same row still fits, and the left column SHALL continue to truncate to the space the right column leaves.

#### Scenario: Bar shown while control is enabled

- **WHEN** control is enabled and the controller demand is roughly half of its range
- **THEN** the footer SHALL show about half the bar's segments filled

#### Scenario: Bar hidden while control is disabled

- **WHEN** control is disabled
- **THEN** no demand bar SHALL be drawn, and the inactive symbol SHALL be shown as before

#### Scenario: Zero demand while enabled

- **WHEN** control is enabled and demand is zero
- **THEN** an empty bar SHALL be drawn rather than no bar

#### Scenario: Date and time still fit

- **WHEN** the bar is drawn alongside a full date and time
- **THEN** the date/time SHALL remain legible, truncated to the room the right column leaves

### Requirement: Demand is quantised with hysteresis before display

Demand SHALL be reduced to a small number of buckets before it reaches the display or the refresh decision, and moving between buckets SHALL require clearing the boundary by a hysteresis margin. Quantisation alone is insufficient: a demand hovering on a boundary would change bucket on alternate ticks and repaint the panel as often as a raw value would.

The refresh policy SHALL treat a change of bucket as a change worth showing, subject to the existing minimum-interval floor, and SHALL compare buckets directly rather than applying a further threshold of its own.

#### Scenario: Bucket holds on a boundary

- **WHEN** the demand fraction sits exactly on a bucket boundary
- **THEN** the displayed bucket SHALL remain whichever it already was

#### Scenario: Dithering does not move the bar

- **WHEN** the demand fraction oscillates by less than the hysteresis margin either side of a boundary
- **THEN** the displayed bucket SHALL NOT change, and no refresh SHALL be triggered by it

#### Scenario: A decisive move changes the bucket

- **WHEN** the demand fraction clears a boundary by more than the hysteresis margin
- **THEN** the displayed bucket SHALL change

#### Scenario: Bucket change triggers a refresh

- **WHEN** the bucket changes and all other displayed values are unchanged
- **THEN** a refresh SHALL be scheduled

#### Scenario: Unchanged bucket triggers nothing

- **WHEN** the bucket is unchanged and no other displayed value has changed
- **THEN** no refresh SHALL be scheduled, even though the raw demand moved

#### Scenario: Bucket changes respect the interval floor

- **WHEN** the bucket changes sooner than the minimum interval allows
- **THEN** the refresh SHALL be deferred, as it is for a setpoint change

#### Scenario: Steady demand costs no refreshes

- **WHEN** the controller output has settled such that the bucket no longer moves
- **THEN** the panel SHALL return to its baseline refresh rate, driven only by the clock and by sensor readings

