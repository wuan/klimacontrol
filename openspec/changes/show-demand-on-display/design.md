# Design: demand bar on the panel

## Layout

The footer's right column is drawn first and fixes how much room the left column
has — an existing property of `runPagedDraw`, which this reuses rather than
disturbs.

```
  --------------------------------------------  <- FOOTER_RULE_Y   152
  Wohnzimmer                          22.0 (o)  <- FOOTER_LINE1_Y   170
  2026-09-02 14:07          [##__]        (*)   <- FOOTER_LINE2_Y   192
                             |             |
                             |             control symbol, 12 px
                             demand bar, 38 px
```

Five segments of 6 px with 2 px gaps is 38 px, plus a 6 px gap to the symbol.
That leaves the date/time about 126 px, and `2026-09-02 14:07` measures roughly
112 px at 9 pt — so it fits, and `fitToWidth` truncates gracefully if a future
format does not.

Segments are outlined when empty and solid when filled, so the scale is legible:
five boxes make "three filled" read as roughly 60 %, where three floating blobs
would not.

## Why the value is bucketed before the policy sees it

Quantising and hysteresis are two different jobs and both are needed.

*Quantising alone* still chatters: a demand sitting near a boundary flips bucket
on alternate ticks and repaints just as often as a raw percentage would.
*Hysteresis alone* on a continuous value does not help either, because any
threshold small enough to be honest is smaller than the tick-to-tick movement.

So `nextDemandBucket(fraction, previous)` does both: it takes the currently
displayed bucket and only moves once the fraction clears the boundary by 3 %.

```
   fraction   0.0      0.2      0.4      0.6      0.8      1.0
   bucket      0   |    1   |    2   |    3   |    4   |    5
                        ^
                        |  moving up needs   > 0.2 + 0.03
                           moving down needs < 0.2 - 0.03
```

That places the hysteresis with the value it smooths, and leaves
`RefreshPolicy` doing what it does for every other field: asking "did this
change?". The policy keeps a plain `!=` rather than a third bespoke threshold
alongside `TEMP_HYSTERESIS_C` and `HUMIDITY_HYSTERESIS_PCT`.

The current bucket lives in `DisplayManager`, not `RefreshPolicy`, because
`nextDemandBucket` needs the previously *displayed* value to apply hysteresis,
and the policy's own `lastDemandBucket` records what was last *painted* — which
diverges whenever the interval floor suppresses a refresh.

## Measured refresh cost

The justification for all of the above is empirical, so it was measured on the
verification unit: 150 s with control enabled and the integral winding up, so
the raw demand moved on essentially every one of 150 PID ticks.

```
  PID ticks in the window                     150
  raw output values                 0.58 -> 0.66 -> 0.98 -> 1.00 (saturated)

  display refreshes                             6
  gaps (s)      10.7  10.7  10.7  49.7  60.7
                |__________________|  |________|
                demand climbing        settled: clock only
                through buckets        (~1/min, the pre-change baseline)
```

Four refreshes carried the whole climb from bucket 3 to bucket 5, floor-limited.
Once the output saturated and the bucket stopped moving, the panel dropped back
to clock-driven refreshes at the pre-change rate.

An exact percentage would have been floor-limited for the entire window — about
fourteen refreshes in the same 150 s — and would have stayed there indefinitely,
because the output keeps dithering even when saturated.

So the bar costs a handful of refreshes during a transient and nothing at all in
steady state. That is what makes it safe to add while
`assess-display-brownout-risk` is still open.

## Note on measuring this

The refresh log line is `ESP_LOGD`, and `CORE_DEBUG_LEVEL` defaults to 0, so it
does not exist in a normal build. The first measurement attempt reported zero
refreshes for that reason alone. Any future attempt needs
`PLATFORMIO_BUILD_FLAGS="-DCORE_DEBUG_LEVEL=4"`.

A related trap: `strings firmware.elf | grep "Partial refresh"` is *not* a valid
check for whether debug logging is compiled in. That literal is passed as an
argument to `noteDuration()`, so it stays in `.rodata` regardless. `"PID: T="`
and `"PID restart"` are inside the log macros and are the reliable canaries.
