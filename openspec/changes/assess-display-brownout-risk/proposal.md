## Why

`add-eink-display` shipped with two power-related items unresolved, deliberately
deferred rather than failed:

- The 100 µF bulk capacitor recommended across the panel's 3V3/GND is **not
  fitted** on the verification unit.
- The multi-hour `Reset reason:` watch for `BROWNOUT` has not been run.

They are one question, not two. The capacitor is only worth fitting if the
panel's refresh transient actually destabilises the rail, and the only way to
know is to observe the device over a realistic period. Deciding either way
without that evidence means soldering a part that may be unnecessary, or
shipping wiring guidance that is quietly wrong.

The concern is not hypothetical. `src/main.cpp` already treats
`ESP_RST_BROWNOUT` as a live suspicion on this board — `resetReasonStr()` exists
specifically because a brownout reset looks like a silent crash with no
backtrace. The display adds a ~25 mA charge-pump load for the ~2.6 s of a full
refresh, which can now coincide with a WiFi TX burst on the same rail. Since the
clock trigger landed, refreshes happen about once a minute rather than rarely,
so the number of opportunities for that coincidence went up by roughly two
orders of magnitude.

## What Changes

- Run a multi-hour observation with the display enabled and no bulk capacitor,
  recording every `Reset reason:` line at boot and correlating any `BROWNOUT`
  against refresh activity.
- Decide the capacitor question from that evidence and record the decision.
- Update `docs/EINK_DISPLAY_WIRING.md` §4 to state the outcome definitively —
  either "required" or "optional, and here is the evidence" — replacing the
  current precautionary "cheap insurance" wording.
- If brownouts are observed, evaluate the cheaper firmware-side mitigations
  before reaching for hardware: suppressing refreshes while the WiFi radio is
  transmitting, or widening the minimum refresh interval.
- Add a `display` spec requirement making the power-transient behaviour and its
  diagnosability explicit, so the reasoning is not rediscovered.

No firmware behaviour changes unless the observation finds a problem.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `display`: a new requirement covering the refresh power transient, the
  conditions under which decoupling is required, and the obligation to keep
  brownout resets diagnosable from the boot log.

## Impact

- **Documentation**: `docs/EINK_DISPLAY_WIRING.md` §4 (power and decoupling) and
  its troubleshooting table.
- **Source**: none expected. Only if brownouts are observed would
  `Display::RefreshPolicy` or `DisplayManager` gain a suppression condition.
- **Hardware**: possibly one 100 µF capacitor per unit.
- **Out of scope**: measuring the supply rail with a scope (the boot log is the
  intended instrument); brownout detector threshold tuning; battery operation.
