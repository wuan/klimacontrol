# Tasks: demand bar on the e-paper panel

## Implementation

- [x] `Display::nextDemandBucket(fraction, previous)` in `RefreshPolicy.h/.cpp`
      — five buckets, 3 % hysteresis at each boundary, NAN and out-of-range
      handled
- [x] `RefreshPolicy::evaluate()` takes the bucket; a change is a change worth
      showing, subject to the existing interval floor
- [x] `commit()` records the painted bucket, so a change suppressed by the floor
      stays outstanding
- [x] `EPaperDisplay::drawDemandBar()` — five segments, outlined when empty and
      solid when filled, on footer line 2 left of the control symbol
- [x] Bar drawn only when `controlState != INACTIVE`; the date/time column
      truncates to what the bar leaves via the existing `fitToWidth`
- [x] `DisplayManager` computes the fraction from `getControlOutput()` and the
      output range, holds the current bucket across ticks, and passes it to both
      the policy and the panel

## Tests

- [x] Bucket 0 and full-scale, NAN, out-of-range inputs
- [x] Rising through every bucket from zero
- [x] Holds on a boundary, from either side
- [x] Dithering across a boundary for 20 ticks never moves the bar
- [x] Moves once the hysteresis margin is cleared, both directions
- [x] Bucket change triggers a refresh
- [x] Unchanged bucket triggers nothing
- [x] Bucket change still respects the interval floor
- [x] `pio test -e native` green (369 cases)

## Verification on hardware

- [x] Firmware builds and flashes
- [x] Refresh cost measured with control enabled and demand modulating:
      6 refreshes in 150 s across 150 PID ticks; gaps 10.7 / 10.7 / 10.7 /
      49.7 / 60.7 s — four during the bucket climb, then back to clock-driven
- [x] Confirms the design claim: a handful of refreshes during a transient,
      baseline rate once settled. An exact percentage would have been
      floor-limited for the whole window and stayed there
- [x] Visually confirmed on the panel: the bar renders as intended and the
      date/time is not truncated at the current font
