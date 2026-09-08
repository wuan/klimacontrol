## Why

The current e-paper display footer uses a single line to show device name, clock, setpoint, and control state. This results in a cluttered layout with the date/time information (currently just HH:MM) taking up space that could be better organized. Moving to a two-line footer separates the device identification (name) from the timestamp (date and time), while maintaining clear visibility of the temperature control status.

## What Changes

- Footer layout changes from single line to two lines
- Left side, line 1: device name
- Left side, line 2: date and time in `YYYY-MM-DD HH:MM` format
- Right side, line 1: setpoint temperature with degree symbol, right-aligned
- Right side, line 2: control state symbol, right-aligned below the setpoint
- Vertical positions: line 1 at y=175, line 2 at y=188, control symbol at y=190
- Horizontal alignment: right side content right-aligned at x=194

## Capabilities

### New Capabilities

*None* — this change modifies existing display behavior rather than introducing new capabilities.

### Modified Capabilities

- `display`: Footer layout changes from single line to two lines. The footer now displays device name on line 1 left, date+time on line 2 left, setpoint with degree symbol on line 1 right, and control state symbol on line 2 right. The setpoint and control symbol are right-aligned instead of centered.

## Impact

- `src/display/EPaperDisplay.cpp` — rendering logic for two-line footer
- `src/display/EPaperDisplay.h` — constant definitions and comments
- `src/display/DisplayManager.cpp` — `formatClock()` updated to produce `YYYY-MM-DD HH:MM`, buffer size increased
