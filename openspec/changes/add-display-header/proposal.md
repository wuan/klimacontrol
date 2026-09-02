## Why

The 1.54" panel has a blank 30-pixel strip along its top edge. It is blank
because `REFRESH_WINDOW_Y = 30` deliberately keeps it outside the partial-refresh
window, and everything the panel currently shows is mutable — a value, a clock, a
setpoint, a control state — so nothing could safely live there. A field left
outside the window freezes between full refreshes, which on a stable sensor is
effectively indefinitely (`EPaperDisplay.cpp:38-49` records that this is why the
footer had to be pulled *inside* the window).

The firmware version is the one string the device shows that is a compile-time
constant. It cannot go stale between refreshes, so the excluded strip is not a
compromise for it — it is the correct home. `RefreshPolicy::evaluate()` makes the
first paint after every boot a `Full` refresh (`RefreshPolicy.cpp:105`),
including the boot that follows an OTA update, so the strip is repainted exactly
when the version can have changed.

The operational value is small but real: after a firmware rollout you can read
which build a device is running by looking at it, without finding its address,
opening the web UI or querying `/api/status`. On a device whose whole purpose is
to be glanced at on a wall, that belongs on the panel.

## What Changes

- Add a header band in the strip `y 0..29`, outside the partial-refresh window:
  - `KlimaControl` flush left — matching the brand casing used by the splash
    and the web UI.
  - `FIRMWARE_VERSION` flush right.

  Both in the built-in 5x7 GFX font, on one row at a glyph top of y=4, so the
  band reads as chrome tucked against the top edge rather than as a heading.
- Draw the version right-aligned first and truncate *it*, not the brand mark,
  should a future version scheme outgrow the 18 characters the band allows.
- Paint the band in the boot splash too, so it is never blank while the device
  is starting, and drop the splash's now-redundant centred `KlimaControl` title.
- No horizontal rule under the band: the footer's rule separates two zones of
  live data; the header is chrome and does not need framing on a 200 px panel.

No new fonts are linked, no per-refresh cost is added (the band is clipped away
on every partial refresh), and no argument is added to `render()` — the version
is a `#define`, read where it is drawn.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `display`: a new requirement for the header band, plus amendments to
  *Displayed content*, *Partial refresh window* and *Boot splash* to account for
  the strip no longer being blank and to record why static content is safe there.

## Impact

- **Source**: `src/display/EPaperDisplay.cpp` (layout constants, `runPagedDraw`,
  `showSplash`), and its include of `OTAConfig.h` for `FIRMWARE_VERSION`.
- **Docs**: the README's E-Paper Display section, which describes the layout.
- **Flash/RAM**: no new font, no new buffer. Page buffer unchanged at 625 B.
- **Out of scope**: a build date or git hash on the panel; making the header
  configurable or the brand mark customisable; showing the version anywhere in
  the value or footer zones; changing `REFRESH_WINDOW_Y`.
