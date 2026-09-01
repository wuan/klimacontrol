# display Specification Delta

## ADDED Requirements

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

## MODIFIED Requirements

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
