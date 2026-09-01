# Tasks: Temperature Control State Symbols

## Backend

- [x] **Add lastControlOutput storage to SensorController**
  - [x] Add `private: float lastControlOutput = 0.0f;` to SensorController.h
  - [x] Initialize `lastControlOutput` in constructor to 0.0f
  - [x] Store output in `updateControl()` before returning

- [x] **Add isControlActive() method to SensorController**
  - [x] Add `public: bool isControlActive() const { return lastControlOutput > 0.0f; }` to SensorController.h

- [x] **Add ControlState enum to EPaperDisplay.h**
  - [x] Define enum class ControlState { INACTIVE, ACTIVE_OFF, ACTIVE_ON }

## API

- [x] **Update StatusRoutes.cpp**
  - [x] Add `doc["control_active"] = sensorController.isControlActive();` to GET /api/status handler
  - [x] Verify JSON serialization works correctly

## Web Interface

- [x] **Add CSS for control symbols**
  - [x] Add `.control-symbol` base class with font-size: 20px, font-weight: bold
  - [x] Add `.control-symbol.inactive` with color: #dc3545 (red)
  - [x] Add `.control-symbol.active-off` with color: #667eea (purple)
  - [x] Add `.control-symbol.active-on` with color: #28a745 (green)

- [x] **Update control.html JavaScript**
  - [x] Add `getControlSymbol(enabled, active)` helper function
  - [x] Add `getControlSymbolClass(enabled, active)` helper function
  - [x] Update `updateStatus()` to use symbol and class instead of text
  - [x] Handle API fallback when `control_active` is undefined

- [x] **Regenerate compressed assets**
  - [x] Run `scripts/compress_web.py` to update control_gz.h

## E-Paper Display

- [x] **Add FreeSans9pt7b font include**
  - [x] Add `#include <Fonts/FreeSans9pt7b.h>` to EPaperDisplay.cpp

- [x] **Add drawControlSymbol helper**
  - [x] Add namespace-level function to draw symbol based on ControlState
  - [x] Inactive: drawFastHLine(x-5, y, 10, GxEPD_BLACK)
  - [x] Active Off: drawCircle(x, y, 6, GxEPD_BLACK)
  - [x] Active On: fillCircle(x, y, 6, GxEPD_BLACK)

- [x] **Update EPaperDisplay::runPagedDraw signature**
  - [x] Add `ControlState controlState` parameter
  - [x] Add `const char *setpointStr` parameter

- [x] **Update runPagedDraw implementation**
  - [x] Set FreeSans9pt7b font for setpoint
  - [x] Measure setpoint text width
  - [x] Center setpoint at x=100
  - [x] Draw control symbol 8px left of setpoint
  - [x] Draw setpoint text
  - [x] Draw degree circle (radius 2px) 4px right of setpoint
  - [x] Reset to built-in font for footer left/right

- [x] **Update EPaperDisplay::render signature and implementation**
  - [x] Add new parameters to render() method
  - [x] Pass through to runPagedDraw()

- [x] **Update DisplayManager::update()**
  - [x] Determine ControlState based on isControlEnabled() and isControlActive()
  - [x] Format setpoint as string (handle NaN case)
  - [x] Pass controlState and setpointStr to panel.render()

## Testing

- [x] **Unit tests for SensorController**
  - [x] Test isControlActive() returns false when disabled
  - [x] Test isControlActive() returns false when at setpoint
  - [x] Test isControlActive() returns true when heating

- [x] **API endpoint test**
  - [x] Verify /api/status returns control_active field
  - [x] Verify control_active is true when heating
  - [x] Verify control_active is false when at setpoint

- [x] **Web interface manual test**
  - [x] Verify symbols display in all three states
  - [x] Verify colors match specification
  - [x] Verify fallback works with old API

- [x] **E-paper display manual test**
  - [x] Verify symbols are drawn correctly
  - [x] Verify setpoint is centered
  - [x] Verify degree circle is visible

- [x] **Integration test**
  - [x] Verify all components work together
  - [x] Verify backwards compatibility

## Documentation

- [x] **Update README or docs if needed**
  - [x] Document new API field
  - [x] Document symbol meanings

## Total Estimated Effort

- Backend: 1-2 hours
- API: 30-60 minutes
- Web Interface: 1-2 hours
- E-Paper Display: 2-3 hours
- Testing: 2-3 hours
- **Total: 7-11 hours**
