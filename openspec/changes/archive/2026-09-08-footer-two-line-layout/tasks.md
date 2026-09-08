## 1. Display Manager Updates

- [x] 1.1 Update `formatClock()` in DisplayManager.cpp to format `YYYY-MM-DD HH:MM` using `Support::formatLocalDate()` and `Support::formatLocalHhMm()`, and verify the formatted string is 16 characters
- [x] 1.2 Increase clock buffer size from 8 to 17 bytes in DisplayManager.cpp and verify compilation succeeds

## 2. E-Paper Display Constants

- [x] 2.1 Add `FOOTER_LINE1_Y = 175` constant to EPaperDisplay.cpp and verify it compiles
- [x] 2.2 Add `FOOTER_LINE2_Y = 188` constant to EPaperDisplay.cpp and verify it compiles
- [x] 2.3 Update comments in EPaperDisplay.cpp to reflect two-line footer layout and verify documentation is accurate

## 3. E-Paper Display Rendering Logic

- [x] 3.1 Modify `runPagedDraw()` to draw device name at (6, FOOTER_LINE1_Y) and verify it appears on line 1 left
- [x] 3.2 Modify `runPagedDraw()` to draw date/time at (6, FOOTER_LINE2_Y) using the updated footerRight string and verify it appears on line 2 left
- [x] 3.3 Modify `runPagedDraw()` to draw setpoint with degree symbol right-aligned at x=194 on line 1 (FOOTER_LINE1_Y) and verify it appears on line 1 right
- [x] 3.4 Modify `runPagedDraw()` to draw control symbol at (188, 190) and verify it appears on line 2 right

## 4. Integration and Verification

- [x] 4.1 Build firmware with `pio run -e adafruit_qtpy_esp32s2` and verify it compiles without errors
- [x] 4.2 Run native tests with `pio test -e native` and verify all tests pass
- [ ] 4.3 Manually verify on hardware that the footer displays: device name on line 1 left, date+time on line 2 left, setpoint+degree on line 1 right, control symbol on line 2 right
- [ ] 4.4 Verify the live clock behavior: the date/time updates on every refresh and shows the current date and time
- [ ] 4.5 Verify control state symbols appear correctly in all three states (Inactive, Active Off, Active On)

## 5. OpenSpec Validation

- [x] 5.1 Run `openspec validate --all --strict` from repo root and verify the change passes validation
