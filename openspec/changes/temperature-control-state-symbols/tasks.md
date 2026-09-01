# Tasks: Temperature Control State Symbols

## Backend

- [ ] **Add lastControlOutput storage to SensorController**
  - [ ] Add `private: float lastControlOutput = 0.0f;` to SensorController.h
  - [ ] Initialize `lastControlOutput` in constructor to 0.0f
  - [ ] Store output in `updateControl()` before returning

- [ ] **Add isControlActive() method to SensorController**
  - [ ] Add `public: bool isControlActive() const { return lastControlOutput > 0.0f; }` to SensorController.h

- [ ] **Add ControlState enum to EPaperDisplay.h**
  - [ ] Define enum class ControlState { INACTIVE, ACTIVE_OFF, ACTIVE_ON }

## API

- [ ] **Update StatusRoutes.cpp**
  - [ ] Add `doc["control_active"] = sensorController.isControlActive();` to GET /api/status handler
  - [ ] Verify JSON serialization works correctly

## Web Interface

- [ ] **Add CSS for control symbols**
  - [ ] Add `.control-symbol` base class with font-size: 20px, font-weight: bold
  - [ ] Add `.control-symbol.inactive` with color: #dc3545 (red)
  - [ ] Add `.control-symbol.active-off` with color: #667eea (purple)
  - [ ] Add `.control-symbol.active-on` with color: #28a745 (green)

- [ ] **Update control.html JavaScript**
  - [ ] Add `getControlSymbol(enabled, active)` helper function
  - [ ] Add `getControlSymbolClass(enabled, active)` helper function
  - [ ] Update `updateStatus()` to use symbol and class instead of text
  - [ ] Handle API fallback when `control_active` is undefined

- [ ] **Regenerate compressed assets**
  - [ ] Run `scripts/compress_web.py` to update control_gz.h

## E-Paper Display

- [ ] **Add FreeSans9pt7b font include**
  - [ ] Add `#include <Fonts/FreeSans9pt7b.h>` to EPaperDisplay.cpp

- [ ] **Add drawControlSymbol helper**
  - [ ] Add namespace-level function to draw symbol based on ControlState
  - [ ] Inactive: drawFastHLine(x-5, y, 10, GxEPD_BLACK)
  - [ ] Active Off: drawCircle(x, y, 6, GxEPD_BLACK)
  - [ ] Active On: fillCircle(x, y, 6, GxEPD_BLACK)

- [ ] **Update EPaperDisplay::runPagedDraw signature**
  - [ ] Add `ControlState controlState` parameter
  - [ ] Add `const char *setpointStr` parameter

- [ ] **Update runPagedDraw implementation**
  - [ ] Set FreeSans9pt7b font for setpoint
  - [ ] Measure setpoint text width
  - [ ] Center setpoint at x=100
  - [ ] Draw control symbol 8px left of setpoint
  - [ ] Draw setpoint text
  - [ ] Draw degree circle (radius 2px) 4px right of setpoint
  - [ ] Reset to built-in font for footer left/right

- [ ] **Update EPaperDisplay::render signature and implementation**
  - [ ] Add new parameters to render() method
  - [ ] Pass through to runPagedDraw()

- [ ] **Update DisplayManager::update()**
  - [ ] Determine ControlState based on isControlEnabled() and isControlActive()
  - [ ] Format setpoint as string (handle NaN case)
  - [ ] Pass controlState and setpointStr to panel.render()

## Testing

- [ ] **Unit tests for SensorController**
  - [ ] Test isControlActive() returns false when disabled
  - [ ] Test isControlActive() returns false when at setpoint
  - [ ] Test isControlActive() returns true when heating

- [ ] **API endpoint test**
  - [ ] Verify /api/status returns control_active field
  - [ ] Verify control_active is true when heating
  - [ ] Verify control_active is false when at setpoint

- [ ] **Web interface manual test**
  - [ ] Verify symbols display in all three states
  - [ ] Verify colors match specification
  - [ ] Verify fallback works with old API

- [ ] **E-paper display manual test**
  - [ ] Verify symbols are drawn correctly
  - [ ] Verify setpoint is centered
  - [ ] Verify degree circle is visible

- [ ] **Integration test**
  - [ ] Verify all components work together
  - [ ] Verify backwards compatibility

## Documentation

- [ ] **Update README or docs if needed**
  - [ ] Document new API field
  - [ ] Document symbol meanings

## Total Estimated Effort

- Backend: 1-2 hours
- API: 30-60 minutes
- Web Interface: 1-2 hours
- E-Paper Display: 2-3 hours
- Testing: 2-3 hours
- **Total: 7-11 hours**
