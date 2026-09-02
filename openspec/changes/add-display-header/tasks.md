## 1. Layout constants

- [x] 1.1 In `src/display/EPaperDisplay.cpp`, add header constants beside the existing layout block: `HEADER_TITLE`, `HEADER_TOP_Y = 4` (one glyph-top constant for the whole row, both fields being in the built-in font) and `HEADER_COLUMN_GAP`
- [x] 1.2 Document in the comment that `HEADER_TOP_Y` is a glyph TOP, not a baseline — the rest of the file uses free fonts, where `setCursor(x, y)` means the baseline, so a reader will otherwise assume wrong
- [x] 1.3 Reuse `FOOTER_MARGIN_X` / `FOOTER_RIGHT_X` for the header's left and right margins rather than introducing parallel constants, so the header and footer stay flush with each other
- [x] 1.4 Confirm on paper that the title's ink (`y 8..24`) and the version's (`y 13..19`) both clear `REFRESH_WINDOW_Y = 30`

## 2. Draw the band

- [x] 2.1 Include `OTAConfig.h` in `EPaperDisplay.cpp` and use `FIRMWARE_VERSION` directly — no new parameter on `render()` or `showSplash()`, per design D4
- [x] 2.2 Add a private `drawHeader()` that paints both fields, with the version laid out first (right-aligned via `drawRightAligned()`) and the brand mark second at the left margin
- [x] 2.3 Truncate the version with `fitToWidth()` against the space left over after the brand mark, so a long `git describe` version degrades instead of colliding; never truncate the brand mark
- [x] 2.4 Select the built-in font once with `setFont(nullptr)` before measuring, since `textWidth()`/`fitToWidth()` measure in the current font; both callers set their own font immediately afterwards
- [x] 2.5 Call `drawHeader()` from `runPagedDraw()` (clipped away on partial refreshes, painted on full ones) — not from `render()` outside the paged loop

## 3. Splash

- [x] 3.1 Call `drawHeader()` from `showSplash()` so the band is not blank during boot
- [x] 3.2 Remove the splash's centred `KlimaControl` title and promote the device name to the 12pt font, keeping the `starting...` line below it in the built-in font
- [x] 3.3 Re-check the splash's remaining baselines after the removal so the two lines are not left with a title-sized gap between them

## 4. Verify on hardware

- [x] 4.1 `pio run -e adafruit_qtpy_esp32s2` and `pio test -e native` (no policy logic changed, but the native env must still build)
- [x] 4.2 Flash and check the splash: brand mark and version in the band, device name and `starting...` below, brand mark appearing exactly once
- [x] 4.3 Check the first value paint: the band survives the transition from splash to values, and the value block geometry is unchanged
- [x] 4.4 Confirm both header fields sit on the same row hard against the top edge (ink `y 4..10`), and that the version reads in full rather than truncated
- [x] 4.5 Watch across at least 12 refreshes so a `Full` refresh occurs, and confirm the band is repainted identically with no ghosting and no drift
- [x] 4.6 Build once with a long `FIRMWARE_VERSION` (e.g. `-DFIRMWARE_VERSION='"v1.2.3-4-gabc1234"'`) and confirm the version truncates while the brand mark stays intact
- [x] 4.7 Confirm the reported page-buffer size in the `E-paper display initialised` log line is unchanged at 625 B

## 5. Document and close out

- [x] 5.1 Update the README's E-Paper Display section with the header band in the layout description
- [x] 5.2 Run `/opsx:verify`, then archive with `/opsx:archive`
