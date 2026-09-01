# Proposal: Temperature Control State Symbols

## Summary

Replace the current text-based temperature control status ("Active" / "Off") with visual symbols that clearly indicate three distinct states: **Inactive** (control disabled), **Active Off** (control enabled but no output), and **Active On** (control enabled and actively heating/cooling). Symbols will be displayed on both the web interface and the e-paper display.

## Motivation

The current web interface shows control status as text ("Active" in green or "Off" in red). This doesn't distinguish between "control is enabled but temperature is at target" (active off) vs "control is enabled and actively adjusting temperature" (active on). For the e-paper display, there is no control status indication at all.

Symbol-based indicators provide:
- **Clearer state distinction**: Three visual states instead of two
- **Consistent UI**: Same symbol system across web and e-paper displays
- **Space efficiency**: Symbols take less space than text, especially valuable on e-paper
- **Internationalization**: Symbols don't require translation

## Proposed Solution

### Symbol Mapping

| State | Description | Web Symbol | E-Paper Symbol | Color (Web) |
|-------|-------------|------------|-----------------|-------------|
| Inactive | Control disabled | `\u2212` (minus sign) | 10px horizontal line | Red (#dc3545) |
| Active Off | Control enabled, output = 0 | `\u25CB` (hollow circle) | 12px hollow circle | Purple (#667eea) |
| Active On | Control enabled, output > 0 | `\u25CF` (filled circle) | 12px filled circle | Green (#28a745) |

### Display Integration

**Web Interface:**
- Replace "Active"/"Off" text in control bar with symbol
- Add CSS styling for symbol colors and sizing
- Maintain API fallback for older clients

**E-Paper Display:**
- Add control state symbol to footer area
- Display setpoint value in center of footer with degree symbol
- Symbol drawn using GFX primitives (circles, lines) since Unicode not available
- Use FreeSans9pt7b font for setpoint text (increased from built-in 6x8)

## Scope

### In Scope
- Add `lastControlOutput` storage to `SensorController`
- Add `isControlActive()` method to `SensorController`
- Add `control_active` boolean field to `/api/status` endpoint
- Update web interface (control.html) to display symbols
- Update e-paper display to draw symbols and show setpoint
- Add CSS styling for web symbols

### Out of Scope
- Adding new control algorithms
- Changing existing temperature control logic
- Adding new sensor types
- MQTT or other protocol updates

## Non-Goals

- Modifying the control algorithm PID parameters
- Adding sound or other feedback mechanisms
- Creating mobile app integration
- Adding historical control state logging

## Success Criteria

1. Web interface displays correct symbol for each control state
2. E-paper display draws correct symbol and shows setpoint in footer
3. API returns `control_active` boolean
4. All existing functionality remains intact
5. Fallback behavior works for clients using old API

## Dependencies

- Existing `SensorController` and `updateControl()` infrastructure
- Existing web interface control bar structure
- Existing e-paper display footer layout
- GxEPD2/GFX library for e-paper drawing primitives

## Open Questions

- Should the symbol be clickable/tappable on web for additional info?
- Should we add a tooltip showing the state name on hover?
- Should the e-paper symbol size be adjustable via configuration?

## Alternatives Considered

1. **Text only**: Keep current text display. Rejected because it doesn't distinguish three states.
2. **Icons only on web**: Add symbols to web but not e-paper. Rejected because user wants consistency across displays.
3. **Different symbols**: Use play/pause/stop metaphors. Rejected as less intuitive for temperature control context.
4. **Color only on e-paper**: Use different text colors. Rejected because e-paper is monochrome.

## Risks

- **E-paper font**: FreeSans9pt7b needs to be added to the project (currently only 12pt and 24pt are included)
- **API backwards compatibility**: Need to ensure old clients still work
- **Symbol visibility**: Need to ensure symbols are clearly visible at small sizes

## Estimated Effort

| Component | Effort |
|-----------|--------|
| SensorController changes | 1-2 hours |
| API endpoint update | 30-60 minutes |
| Web interface update | 1-2 hours |
| E-paper display update | 2-3 hours |
| Testing and validation | 2-3 hours |
| **Total** | **7-11 hours** |
