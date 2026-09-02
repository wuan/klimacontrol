# Show controller demand on the e-paper panel

## Why

The panel's footer reports the controller with a single symbol: `−`, `○` or `●`.
`●` means `lastControlOutput > 0.0f`, so it looks identical at 1 % demand and at
100 %. With no actuator built, the computed demand is the only observable result
of the control loop, and the panel — the thing actually visible in the room —
cannot show it.

`show-control-parameters` put the number in the web UI. The panel wants the same
information, but it cannot have it the same way.

## The constraint that shapes this

`RefreshPolicy` repaints whenever a displayed value changes, subject to a
minimum-interval floor (10 s on the verification unit). A live demand percentage
changes on essentially every control tick, so displaying one would hold the
panel at that floor permanently:

```
  today   idle   ~1 refresh/min      (clock minute rolls over)
          busy   ~1 per 10 s         (only while values genuinely move)

  live %         ~1 per 10 s, forever   — roughly 6x idle, permanently
```

That runs straight into `assess-display-brownout-risk`, still open, whose
premise is that the clock trigger already raised refresh frequency by about two
orders of magnitude and made the panel's ~25 mA transient coincide with WiFi TX
far more often. Multiplying it again before that question is answered would be
working against an open investigation.

There is also no room. The footer is a full two-line, two-column grid — name |
setpoint, date/time | symbol — with the rule at y=152 and baselines at 170 and
192 on a 200 px panel. There is no third line.

## What Changes

- Demand is shown as a **five-segment bar** on footer line 2, between the
  date/time and the control symbol. No new row; the date/time column truncates
  to what is left, using the existing `fitToWidth` mechanism.
- Demand is **quantised with hysteresis** before it reaches the display, by a
  new `Display::nextDemandBucket()`. Five buckets, 3 % hysteresis at each
  boundary — the same reasoning as the existing `TEMP_HYSTERESIS_C` and
  `HUMIDITY_HYSTERESIS_PCT`, applied to bucket edges so a demand hovering on a
  boundary does not repaint every tick.
- `RefreshPolicy::evaluate()` takes the bucket and treats a change as worth
  showing, exactly like the setpoint and the control symbol. Because the value
  arrives already hysteretic, the policy does a plain comparison.
- The bar is drawn only while control is enabled. When it is off the `−` symbol
  already says everything and an empty bar would be clutter; when it is on the
  bar is drawn even at zero demand, because "enabled and asking for nothing" is
  worth distinguishing from "switched off".

## Non-goals

- **Showing the gains on the panel.** They are compile-time constants that never
  change, so they would consume scarce glanceable space to display information
  that is static and already in the web UI.
- **An exact percentage.** Rejected on the refresh-cost grounds above.
- **A second screen.** No screen-switching mechanism exists, and inventing one
  to display static values is not worth it.
- Any change to what `control_active` means, or to the symbol itself.

## Capabilities

### Modified Capabilities

- `display`: the footer gains a quantised demand bar, and the refresh decision
  gains the demand bucket as an input.

## Impact

- **Source**: `src/display/RefreshPolicy.{h,cpp}` (bucketing helper, new
  evaluate parameter), `src/display/EPaperDisplay.{h,cpp}` (bar geometry and
  drawing), `src/display/DisplayManager.{h,cpp}` (computes the bucket, holds it
  across ticks).
- **Tests**: nine new cases in `test/test_display_refresh_policy`.
- **Refresh load**: measured, not assumed. See `design.md`.
