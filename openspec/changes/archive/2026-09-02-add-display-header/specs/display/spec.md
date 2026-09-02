## ADDED Requirements

### Requirement: Header band with brand mark and firmware version

The e-paper layout SHALL include a header band occupying the region `(0, 0)` to
`(199, 29)` — the strip above the partial-refresh window — containing exactly
two fields, both rendered in the built-in 5x7 GFX font:

- `KlimaControl` flush left at the panel's left margin (x=6). The casing SHALL
  match the splash and the web UI.
- The firmware version (`FIRMWARE_VERSION`) flush right at the panel's right
  margin (x=194), on the same row.

Both fields SHALL sit at a glyph top of y=4, tucked against the panel's top
edge. Because the built-in font takes `setCursor(x, y)` as the glyph **top**
while the free GFX fonts used elsewhere in the layout take it as the
**baseline**, a single shared constant SHALL express the row position, and it
SHALL NOT be reinterpreted as a baseline.

No horizontal rule SHALL be drawn beneath the band. The band SHALL NOT contain
any further field, and SHALL leave the value block and footer regions unchanged.

The version field SHALL be laid out first, right-aligned, and the version string
SHALL be the field that is truncated should the two fields not both fit. The
brand mark is a fixed string and SHALL NOT be truncated. Truncation SHALL use the
same trailing-`.` marker as the footer fields, keeping the release prefix of a
`git describe` version and dropping its build suffix. With both fields in the
small font the 12-character brand mark leaves 110 px — 18 characters — for the
version, so truncation is a guard rather than the expected path.

#### Scenario: Tagged release build

- **WHEN** the firmware is built from the tag `v0.1.1` and the display is enabled
- **THEN** the panel SHALL show `KlimaControl` in the top-left corner and `v0.1.1` in the top-right corner, both in the small font on the same row

#### Scenario: Developer version fits unabbreviated

- **WHEN** `FIRMWARE_VERSION` is a `git describe` string such as `v0.1.1-5-gc1c08f0`
- **THEN** the panel SHALL show it in full beside the brand mark

#### Scenario: Version longer than the band allows

- **WHEN** `FIRMWARE_VERSION` is wider than the space left over by the brand mark
- **THEN** the version SHALL be truncated with a trailing `.` and the brand mark SHALL be rendered in full and unmoved

#### Scenario: Header does not intrude on the value block

- **WHEN** the header band is painted
- **THEN** no header ink SHALL be drawn at y >= 30, so the value block and the partial-refresh window are unaffected

### Requirement: Header band content must be static

Only content that cannot change while the firmware runs SHALL be placed in the
header band, because the band lies outside the partial-refresh window and is
therefore rewritten only by a `Full` refresh.

The firmware version satisfies this: it is a compile-time constant, and the first
paint after every boot — including the boot that follows an OTA update — is a
`Full` refresh, so the band is repainted exactly when its content can have
changed.

If a field in the band ever becomes runtime-mutable, the band SHALL be moved
inside the partial-refresh window rather than left to freeze. In particular the
device name SHALL NOT be moved into the band, as the web UI can change it without
a reboot.

#### Scenario: Version updates after an over-the-air update

- **WHEN** a device is updated over the air and reboots onto the new firmware
- **THEN** the first paint SHALL be a `Full` refresh and the header band SHALL show the new version

#### Scenario: Band survives partial refreshes untouched

- **WHEN** a sequence of `Partial` refreshes repaints the values and footer
- **THEN** the header band SHALL be left unwritten, retaining the pixels from the last `Full` refresh, and SHALL add no drawing cost to those refreshes

## MODIFIED Requirements

### Requirement: Displayed content

The display SHALL show the current temperature and the current relative
humidity, sourced from `SensorController` under a single consistent snapshot.
No other measurement type SHALL be rendered.

The layout SHALL place the header band described in *Header band with brand mark
and firmware version* in the region `(0, 0)` to `(199, 29)`, the temperature as
the primary value and the humidity below it, with the two-line footer described
in *Two-line footer layout*. The value block SHALL occupy the region `(0, 30)` to
`(199, 139)`; the footer SHALL occupy the region below it, from the rule at y=152
to the bottom of the panel.

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

#### Scenario: Header is present alongside the values

- **WHEN** the panel shows a temperature and humidity reading
- **THEN** the header band SHALL be visible above them, and the value block SHALL keep its existing geometry

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

The strip above the window, `y 0..29`, holds the header band. Drawing commands
targeting it are clipped away while the partial window is active, so the band
costs nothing on a partial refresh; it SHALL therefore carry only static content,
as required by *Header band content must be static*.

#### Scenario: Partial refresh does not flash

- **WHEN** the policy returns `Partial`
- **THEN** the region SHALL be rewritten without the black/white inversion flash of a full refresh

#### Scenario: Clock stays in step with the reading

- **WHEN** a partial refresh repaints the temperature and humidity
- **THEN** both footer lines SHALL be repainted in the same operation

#### Scenario: A stable sensor still advances the clock

- **WHEN** the measured values remain inside the hysteresis band for an extended period
- **THEN** the panel SHALL still refresh as the minute rolls over, subject to the configured minimum interval

#### Scenario: Header is clipped rather than redrawn

- **WHEN** the paged draw loop runs with the partial window active
- **THEN** the header band's drawing commands SHALL be clipped, leaving the panel's existing header pixels in place

### Requirement: Boot splash

When the display is enabled, the firmware SHALL paint a splash screen at the end
of `setup()` showing the device name and an indication that the device is
starting, before any sensor reading is available.

The splash SHALL also paint the header band, so the brand mark and the firmware
version are visible throughout boot — including a boot that does not complete.
Because the band carries the brand mark, the splash SHALL NOT additionally render
a centred `KlimaControl` title: the splash body is the device name over the
"starting" indication.

#### Scenario: Booting device is visibly distinct from a dead one

- **WHEN** a device with the display enabled powers on
- **THEN** the panel SHALL show the device name and a "starting" indication within the boot sequence, and SHALL be replaced by the first measurement once a valid sensor snapshot exists

#### Scenario: Version is readable during a failed boot

- **WHEN** a device with the display enabled powers on and does not reach its first measurement
- **THEN** the panel SHALL still show the header band, so the running firmware version can be read from the device

#### Scenario: Brand mark appears once

- **WHEN** the splash is painted
- **THEN** `KlimaControl` SHALL appear only in the header band, and the splash body SHALL show the device name and the "starting" indication
