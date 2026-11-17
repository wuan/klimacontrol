# Spec Delta — display

## ADDED Requirements

### Requirement: Configurable display value fields

`DisplayConfig` SHALL expose two configurable value slots. The second slot SHALL always be configured and SHALL default to relative humidity. The third slot SHALL be optional and SHALL default to none. Supported selections SHALL be humidity, dew point, pressure, sea-level pressure, CO₂, illuminance, VOC index and PM2.5. Temperature SHALL remain fixed as the primary value.

#### Scenario: Default configuration

- **WHEN** a device boots with no stored display value selections
- **THEN** the second slot SHALL be relative humidity, the third slot SHALL be none, and the panel SHALL use the two-line layout

#### Scenario: Dew point as the second value

- **WHEN** the second slot is configured as dew point and the third slot as none
- **THEN** the panel SHALL show temperature and dew point, with no third value line

#### Scenario: Pressure as the third value

- **WHEN** the second slot is humidity and the third slot is pressure
- **THEN** the panel SHALL show temperature, humidity and pressure using the three-line layout

### Requirement: Display field labels

Each configured value SHALL be rendered with a fixed label. Humidity SHALL use `%rH`; dew point SHALL use `DP` with a drawn degree ring; pressure SHALL use `P`; sea-level pressure SHALL use `SLP`; CO₂ SHALL use `CO2`; illuminance SHALL use `lux`; VOC index SHALL use `VOC`; PM2.5 SHALL use `PM2.5`. Units SHALL be `hPa` for pressure values, `ppm` for CO₂, `lux` for illuminance and `ug/m3` for PM2.5.

#### Scenario: Humidity line

- **WHEN** humidity is 47 %RH
- **THEN** the line SHALL render `47 %rH`

#### Scenario: Dew point line

- **WHEN** dew point is 12.4 °C
- **THEN** the line SHALL render `DP 12.4` followed by the drawn degree ring

#### Scenario: Pressure and sea-level pressure lines

- **WHEN** pressure is 1013 hPa and sea-level pressure is 1016 hPa
- **THEN** the pressure line SHALL render `P 1013 hPa` and the sea-level line SHALL render `SLP 1016 hPa`

#### Scenario: CO₂ line

- **WHEN** CO₂ concentration is 812 ppm
- **THEN** the line SHALL render `CO2 812 ppm`

#### Scenario: VOC index line

- **WHEN** the VOC index is 120
- **THEN** the line SHALL render `VOC 120`

#### Scenario: PM2.5 line uses the ASCII unit

- **WHEN** PM2.5 concentration is 8 µg/m³
- **THEN** the line SHALL render `PM2.5 8 ug/m3`

### Requirement: Display field precision

Temperature and dew point SHALL use one decimal place. Humidity, pressure, sea-level pressure, CO₂, illuminance, VOC index and PM2.5 SHALL use whole numbers.

#### Scenario: Decimal fields

- **WHEN** temperature is 21.4 °C and dew point is 12.4 °C
- **THEN** both SHALL render to one decimal place

#### Scenario: Whole-number fields

- **WHEN** humidity is 47 %RH, pressure is 1013 hPa and CO₂ is 812 ppm
- **THEN** all three SHALL render as whole numbers

### Requirement: Display field availability

A configured value SHALL be available only when the sensor snapshot is valid and that value's measurement exists in it. Sea-level pressure SHALL additionally require a configured elevation greater than zero. VOC index SHALL additionally require temperature and relative humidity for SGP40 compensation. An unavailable configured value SHALL render `--` in place of its numeric part while retaining its label and unit.

#### Scenario: Pressure sensor absent

- **WHEN** pressure is selected but no pressure-capable sensor is configured
- **THEN** the pressure line SHALL render its placeholder rather than a stale or zero value

#### Scenario: Sea-level pressure without elevation

- **WHEN** sea-level pressure is selected and the configured elevation is zero
- **THEN** the sea-level pressure line SHALL render its placeholder

#### Scenario: VOC index without compensation inputs

- **WHEN** VOC index is selected and temperature or relative humidity is unavailable
- **THEN** the VOC index line SHALL render its placeholder

#### Scenario: Invalid snapshot

- **WHEN** the sensor snapshot is invalid
- **THEN** every configured value SHALL render its placeholder while the layout SHALL remain unchanged

### Requirement: Configured value slots drive the refresh decision

The refresh decision SHALL treat each configured value slot independently. A slot whose value changes by at least its field-specific hysteresis SHALL be a change worth showing; a slot becoming available or unavailable SHALL likewise be a change. The thresholds SHALL be humidity 1.0 %RH, dew point 0.1 °C, pressure and sea-level pressure 0.5 hPa, CO₂ 10 ppm, illuminance 5 lux, VOC index 5, and PM2.5 1 µg/m³. A missing slot SHALL NOT suppress changes in another slot.

#### Scenario: VOC jitter below the threshold

- **WHEN** the VOC index moves by fewer than 5 points and no other displayed value changes
- **THEN** the policy SHALL return `None`

#### Scenario: VOC index decisive change

- **WHEN** the VOC index moves by at least 5 points and no other displayed value changes
- **THEN** the policy SHALL return a refresh once the minimum interval has passed

#### Scenario: Pressure-only sensor

- **WHEN** temperature and humidity are unavailable but a pressure sensor reports a valid pressure change
- **THEN** the pressure slot SHALL still trigger a refresh, and the temperature and humidity lines SHALL keep their placeholders

#### Scenario: Slot availability transition

- **WHEN** a configured slot transitions between a reading and its placeholder
- **THEN** the policy SHALL schedule a refresh subject to the minimum-interval floor

#### Scenario: Unchanged slot triggers nothing

- **WHEN** a slot's value stays inside its hysteresis band and its availability is unchanged
- **THEN** no refresh SHALL be scheduled on that slot's account

#### Scenario: Slot change respects the interval floor

- **WHEN** a configured slot changes sooner than the minimum interval allows
- **THEN** the refresh SHALL be deferred until the floor passes, and the outstanding change SHALL still be rendered

### Requirement: Two-line and three-line value layout

The value block SHALL use the two-line layout when the third slot is none and the three-line layout otherwise. The two-line layout SHALL place the temperature baseline at 81 and the second line at 124. The three-line layout SHALL place the temperature baseline at 73, the second line at 107 and the third line at 135. Temperature SHALL use `FreeSansBold24pt7b`; both secondary lines SHALL use `FreeSans12pt7b`. The value block SHALL remain `(0, 30)` to `(199, 139)`.

#### Scenario: Two-line geometry

- **WHEN** the third slot is none
- **THEN** the panel SHALL use the two-line layout and the second line SHALL sit below the temperature with the existing spacing

#### Scenario: Three-line geometry

- **WHEN** the third slot is configured
- **THEN** the panel SHALL use the three-line layout with the second line at baseline 107 and the third at baseline 135

#### Scenario: Descenders stay inside the value block

- **WHEN** the third line contains a descender such as the `p` in `hPa`
- **THEN** its descender SHALL stay within the value block boundary at y=139

#### Scenario: Missing reading does not change layout

- **WHEN** a configured value is unavailable
- **THEN** the panel SHALL keep the layout selected by configuration and render a placeholder rather than collapsing to the other layout

#### Scenario: Footer remains unchanged

- **WHEN** either value layout is rendered
- **THEN** the footer rule SHALL remain at y=152 and its text baselines at y=170 and y=192

## MODIFIED Requirements

### Requirement: Displayed content

The display SHALL show the current temperature, the configured second value, and the configured optional third value, read from `SensorController` under a single consistent snapshot. Temperature SHALL be the primary value; the second value SHALL appear below it; the third value, when configured, SHALL appear below the second. No measurement beyond the configured fields SHALL be rendered.

The layout SHALL place the values in the value block `(0, 30)` to `(199, 139)`, with a two-line footer below it. The footer SHALL display the device name on line 1 left and the date and time in `YYYY-MM-DD HH:MM` format on line 2 left. The footer SHALL display the setpoint temperature with degree symbol on line 1 right and the control state symbol on line 2 right, both right-aligned.

When a displayed value is unavailable — the sensor snapshot is invalid, the value is absent, or the accessor returns `NAN` — the firmware SHALL render the value's placeholder rather than a stale or zero value.

#### Scenario: Both values available

- **WHEN** the sensor snapshot is valid and reports 21.4 °C and 47 %RH with humidity selected second and no third value
- **THEN** the panel SHALL show the temperature to one decimal place and humidity as a whole number, with the temperature rendered in the larger font

#### Scenario: Configured second field

- **WHEN** dew point is selected second and the snapshot reports 12.4 °C dew point
- **THEN** the panel SHALL show temperature and dew point in the two-line layout

#### Scenario: Configured third field

- **WHEN** pressure is selected third and the snapshot reports 1013 hPa
- **THEN** the panel SHALL show temperature, the second value and pressure in the three-line layout

#### Scenario: No sensor attached

- **WHEN** the display is enabled but no sensor is configured, so temperature and the configured values are unavailable
- **THEN** the panel SHALL render placeholders rather than numeric values

#### Scenario: Values are read atomically

- **WHEN** the display gathers the values to render
- **THEN** it SHALL use `SensorController::getSnapshot()` (or an equivalent single-lock accessor) so temperature, the configured values and the validity flag describe the same instant

#### Scenario: Setpoint unavailable

- **WHEN** the target temperature is `NAN`
- **THEN** the setpoint field SHALL render `--` rather than a number

#### Scenario: Header is present alongside the values

- **WHEN** the panel shows the configured values
- **THEN** the header band SHALL be visible above them, and the value block SHALL keep its configured geometry

### Requirement: Refresh policy

The firmware SHALL decide between no refresh, a partial refresh, and a full refresh using a pure decision function that takes the temperature, the configured second and third values with their availability and hysteresis, the current millisecond clock, the displayed setpoint, the control state, the demand bucket and the warning token, and returns one of `RefreshKind::{None, Partial, Full}`.

The policy SHALL apply, in order:

1. **First paint** — the first evaluation after boot SHALL return `Full`.
2. **Change detection** — a refresh SHALL be considered when any of the following differs from what is currently rendered: the temperature has moved at least 0.1 °C; a configured value has moved by at least its field-specific hysteresis; a configured value's availability has changed in either direction; the wall-clock minute has rolled over; the setpoint, control state, demand bucket or warning token has changed.
3. **Minimum interval** — a refresh SHALL NOT occur within `DisplayConfig::interval` seconds of the previous refresh. A change suppressed by this floor SHALL be re-evaluated on subsequent ticks rather than discarded.
4. **Ghosting** — every 12th consecutive partial refresh SHALL be promoted to a full refresh, resetting the counter.

Elapsed-time comparisons SHALL use unsigned subtraction so the policy behaves correctly across the `millis()` rollover.

The decision function SHALL be free of Arduino and FreeRTOS dependencies and SHALL be compiled and unit-tested in the `native` PlatformIO environment.

#### Scenario: First evaluation is a full refresh

- **WHEN** the policy evaluates for the first time after boot
- **THEN** it SHALL return `Full` so the retained panel image is replaced

#### Scenario: Noise below the hysteresis threshold

- **WHEN** temperature moves by less than 0.1 °C, each configured value stays inside its hysteresis band, and the clock minute is unchanged
- **THEN** the policy SHALL return `None` and the panel SHALL NOT be refreshed

#### Scenario: The clock advances on its own

- **WHEN** the wall-clock minute rolls over and no measured value has changed
- **THEN** the policy SHALL return a refresh, so the footer shows a live clock rather than the timestamp of the last reading

#### Scenario: The clock cannot outrun the refresh budget

- **WHEN** the configured minimum interval is longer than 60 seconds
- **THEN** clock-driven refreshes SHALL still be subject to that floor, so the displayed time may lag by up to one interval

#### Scenario: Clock-driven refreshes count toward the ghosting budget

- **WHEN** a refresh is triggered solely by the clock advancing
- **THEN** it SHALL advance the partial counter exactly as a value-driven refresh does

#### Scenario: Change inside the minimum interval

- **WHEN** a configured value has moved beyond its threshold but only 20 seconds have elapsed since the last refresh and `interval` is 60
- **THEN** the policy SHALL return `None`, and once 60 seconds have elapsed the still-outstanding change SHALL produce a refresh

#### Scenario: Periodic full refresh clears ghosting

- **WHEN** the configured partial-refresh budget has been exhausted and a further refresh is due
- **THEN** the policy SHALL return `Full` and reset the partial counter

#### Scenario: Validity transition forces a refresh

- **WHEN** a configured value moves from available to unavailable, or from unavailable to available
- **THEN** the change SHALL bypass the value hysteresis check so the panel switches between the reading and the placeholder promptly

#### Scenario: Clock rollover

- **WHEN** `millis()` wraps from near `UINT32_MAX` to a small value between two evaluations
- **THEN** the minimum-interval check SHALL still compute a correct elapsed time and SHALL NOT suppress refreshes for the following 49 days

### Requirement: Warning icon layout

The e-paper value block SHALL include warning slots in both margins — the left margin and its mirror image in the right margin: a filled warning triangle with the panel's drawn-symbol technique and the active warning's label in the built-in 5x7 font directly below it. Both slots SHALL show the same token.

The warning triangle SHALL be 34 px tall and 34 px wide at its base, anchored to the temperature baseline: its apex SHALL sit 34 px above that baseline, its base SHALL coincide with it, and the label's glyph top SHALL sit 6 px below it. In the two-line layout the triangle SHALL occupy y 47..81 with its label at y=87; in the three-line layout it SHALL occupy y 39..73 with its label at y=79. The slot centres SHALL be x=31 and the mirror image at x=169.

Each slot SHALL lie entirely inside the partial-refresh window. The temperature, all configured value lines and the footer SHALL keep their geometry and position whether or not the warning slots are occupied — the warning is purely additive, and showing or clearing it SHALL NOT shift or rescale any other element. The warning label SHALL sit above the second value line so a long second value cannot collide with it.

#### Scenario: Warning shown with a long second line

- **WHEN** a warning is active and the second line is a wide value such as `SLP 1013 hPa`
- **THEN** the warning triangle and label SHALL remain in both margins above the second line, and the second line SHALL not be shifted or overwritten

#### Scenario: Warning follows the temperature in the three-line layout

- **WHEN** the three-line layout is rendered
- **THEN** the warning triangle SHALL move up with the temperature to y 39..73 and its label to y=79

#### Scenario: Warning shown beside unchanged values

- **WHEN** a warning becomes active while the readings are stable
- **THEN** the panel SHALL show the triangle and label in both margins, and the temperature, configured values and footer SHALL remain in their existing positions

#### Scenario: Warning cleared restores the empty margins

- **WHEN** the active warning clears
- **THEN** both margins SHALL be blank again and no other element SHALL have moved

#### Scenario: Warning refresh is partial, not full

- **WHEN** a warning appears or clears and no other displayed value has changed
- **THEN** the repaint SHALL be a partial refresh inside the existing window, and SHALL NOT introduce a full-panel refresh

### Requirement: Partial refresh window

A `Partial` refresh SHALL update the region `(0, 30)` 200×170 via `setPartialWindow()`, i.e. down to the bottom edge of the panel. This window SHALL contain every element that can change between refreshes: all configured value lines and the footer.

The footer SHALL be a **live wall clock**. It SHALL be redrawn on every refresh, partial included, and the minute rolling over SHALL itself trigger a refresh (see *Refresh policy*), so the displayed time tracks real time rather than only the moment of the last reading.

The window was widened from 160 to 170 rows when the footer became two lines tall: a field left outside it would freeze between full refreshes, which on a stable sensor could be indefinitely.

The strip above the window, `y 0..29`, holds the header band. Drawing commands targeting it are clipped away while the partial window is active, so the band costs nothing on a partial refresh; it SHALL therefore carry only static content, as required by *Header band content must be static*.

#### Scenario: Partial refresh does not flash

- **WHEN** the policy returns `Partial`
- **THEN** the region SHALL be rewritten without the black/white inversion flash of a full refresh

#### Scenario: Clock stays in step with the reading

- **WHEN** a partial refresh repaints the configured value lines
- **THEN** both footer lines SHALL be repainted in the same operation

#### Scenario: Three-line values stay inside the window

- **WHEN** the three-line layout is rendered on a partial refresh
- **THEN** the second and third value lines, the warning slots and the footer SHALL all be inside the partial-refresh window

#### Scenario: A stable sensor still advances the clock

- **WHEN** the measured values remain inside their hysteresis bands for an extended period
- **THEN** the panel SHALL still refresh as the minute rolls over, subject to the configured minimum interval

#### Scenario: Header is clipped rather than redrawn

- **WHEN** the paged draw loop runs with the partial window active
- **THEN** the header band's drawing commands SHALL be clipped, leaving the panel's existing header pixels in place
