# display Specification Delta

## ADDED Requirements

### Requirement: Control state symbol on e-paper display

The e-paper display SHALL show the temperature control state using drawn symbols in the footer area. The footer SHALL display the setpoint temperature centered horizontally at x=100, with a control state symbol to its left and a degree symbol (small circle) to its right. The control state symbol SHALL be drawn using GFX primitives: a 10px horizontal line for **Inactive**, a 12px diameter hollow circle for **Active Off**, or a 12px diameter filled circle for **Active On**. The setpoint text SHALL use the FreeSans9pt7b font.

#### Scenario: Inactive state shows minus line

- **WHEN** control is disabled
- **THEN** the footer SHALL display a 10px wide horizontal line centered vertically in the footer area

#### Scenario: Active Off state shows hollow circle

- **WHEN** control is enabled and output is zero
- **THEN** the footer SHALL display a hollow circle with 6px radius (12px diameter)

#### Scenario: Active On state shows filled circle

- **WHEN** control is enabled and output is non-zero
- **THEN** the footer SHALL display a filled circle with 6px radius (12px diameter)

#### Scenario: Setpoint displayed in center

- **WHEN** the display is rendering
- **THEN** the setpoint temperature value SHALL be centered horizontally at x=100 in the footer area

#### Scenario: Degree symbol drawn as circle

- **WHEN** the setpoint is displayed
- **THEN** a small circle with 2px radius SHALL be drawn immediately to the right of the setpoint text to indicate degrees Celsius

#### Scenario: Symbol and setpoint on same line

- **WHEN** the footer is rendered
- **THEN** the control state symbol, setpoint text, and degree circle SHALL all appear on the same horizontal line
