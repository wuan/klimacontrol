## MODIFIED Requirements

### Requirement: Displayed content

The display SHALL show the current temperature and the current relative
humidity, sourced from `SensorController` under a single consistent snapshot.
No other measurement type SHALL be rendered.

The layout SHALL place the temperature as the primary value and the humidity
below it, with a two-line footer. The value block SHALL occupy the region
`(0, 30)` to `(199, 139)`; the footer SHALL occupy the region below it with
line 1 at y=175 and line 2 at y=188.

The footer SHALL display the device name on line 1 left and the date and time
in `YYYY-MM-DD HH:MM` format on line 2 left. The footer SHALL display the
setpoint temperature with degree symbol on line 1 right and the control state
symbol on line 2 right, both right-aligned.

When a value is unavailable — the sensor snapshot is invalid, or the accessor
returns `NAN` — the firmware SHALL render a placeholder (`--.-` for temperature,
`--` for humidity) rather than a stale or zero value.

#### Scenario: Both values available

- **WHEN** the sensor snapshot is valid and reports 21.4 °C and 47 %RH
- **THEN** the panel SHALL show the temperature to one decimal place and the humidity as a whole number, with the temperature rendered in the larger font

#### Scenario: No sensor attached

- **WHEN** the display is enabled but no sensor is configured, so `getTemperature()` returns `NAN`
- **THEN** the panel SHALL render the placeholder text rather than a numeric value

#### Scenario: Values are read atomically

- **WHEN** the display gathers the values to render
- **THEN** it SHALL use `SensorController::getSnapshot()` (or an equivalent single-lock accessor) so temperature, humidity and the validity flag describe the same instant

### Requirement: Control state symbol on e-paper display

The e-paper display SHALL show the temperature control state using drawn symbols in the footer area. The footer SHALL display the setpoint temperature on line 1 right, with a degree symbol (small circle) to its right. The control state symbol SHALL be drawn on line 2 right, right-aligned below the setpoint line. The control state symbol SHALL be drawn using GFX primitives: a 10px horizontal line for **Inactive**, a 12px diameter hollow circle for **Active Off**, or a 12px diameter filled circle for **Active On**. The setpoint text SHALL use the FreeSans9pt7b font.

#### Scenario: Inactive state shows minus line

- **WHEN** control is disabled
- **THEN** the footer SHALL display a 10px wide horizontal line on line 2 right, right-aligned

#### Scenario: Active Off state shows hollow circle

- **WHEN** control is enabled and output is zero
- **THEN** the footer SHALL display a hollow circle with 6px radius (12px diameter) on line 2 right, right-aligned

#### Scenario: Active On state shows filled circle

- **WHEN** control is enabled and output is non-zero
- **THEN** the footer SHALL display a filled circle with 6px radius (12px diameter) on line 2 right, right-aligned

#### Scenario: Setpoint displayed in center

- **WHEN** the display is rendering
- **THEN** the setpoint temperature value SHALL be displayed on line 1 right, right-aligned, with a degree symbol circle to its right

#### Scenario: Degree symbol drawn as circle

- **WHEN** the setpoint is displayed
- **THEN** a small circle with 2px radius SHALL be drawn immediately to the right of the setpoint text to indicate degrees Celsius

#### Scenario: Control state symbol below setpoint

- **WHEN** the footer is rendered
- **THEN** the control state symbol SHALL appear on line 2 right, right-aligned, below the setpoint text and degree circle

### Requirement: Partial refresh window

A `Partial` refresh SHALL update the region `(0, 30)` 200×160 via
`setPartialWindow()`. This window SHALL contain every element that can change
between refreshes: both value lines and the footer.

The footer's right-hand field SHALL be a **live wall clock**. It SHALL be
redrawn on every refresh, partial included, and the minute rolling over SHALL
itself trigger a refresh (see *Refresh policy*), so the displayed time tracks
real time rather than only the moment of the last reading. The footer now displays
date and time on line 2 left, which also SHALL be redrawn on every refresh.

#### Scenario: Partial refresh does not flash

- **WHEN** the policy returns `Partial`
- **THEN** the region SHALL be rewritten without the black/white inversion flash of a full refresh

#### Scenario: Clock stays in step with the reading

- **WHEN** a partial refresh repaints the temperature and humidity
- **THEN** the footer date/time and control symbol SHALL be repainted in the same operation

#### Scenario: A stable sensor still advances the clock

- **WHEN** the measured values remain inside the hysteresis band for an extended period
- **THEN** the panel SHALL still refresh as the minute rolls over, subject to the configured minimum interval
