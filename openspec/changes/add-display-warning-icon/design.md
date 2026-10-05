# Design — add-display-warning-icon

## Context

The e-paper pipeline is `DisplayManager::update()` (Network task, 1 s tick) →
`RefreshPolicy::evaluate()` (native-testable decision function) →
`EPaperDisplay::render()` (paged draw). The policy already treats
setpoint/control-state/demand-bucket changes as "worth showing" via dedicated
requirements, each added as its own spec requirement rather than by rewriting
the base refresh-policy requirement — this change follows the same pattern.

Condition sources all exist today:

- Sensor invalid: `SensorController::getSnapshot().valid`
- Over-temp trip: `TemperatureController::isSafetyShutoffEngaged()`
- Actuator uncertain: `ControlState::UNCERTAIN` (already computed for the footer)
- Thresholds: raw temp/humidity from the same snapshot

Constraints that shape the design: values must not move (partial-refresh
friendliness); no new full-refresh triggers (brownout-risk change is still
open); GFX free fonts carry only 0x20–0x7E, so both icon and label are drawn;
`render()` already takes 8 parameters.

## Goals / Non-Goals

**Goals:**

- One visible warning (icon + label) at any time, additive to the layout
- Warning decision logic native-testable, same discipline as RefreshPolicy
- Threshold warnings configurable, persisted, exposed via `/api/display`

**Non-Goals:**

- WiFi/MQTT-down warnings (deferred)
- Rotation across simultaneous warnings; free-text labels; takeover screens
- Web-UI-side changes beyond adding threshold inputs to the existing
  E-Paper Display section

## Decisions

### D1: New `Display::WarningPolicy` sibling to `RefreshPolicy`

Pure C++, no Arduino/FreeRTOS includes, caller supplies the clock — identical
shape to `RefreshPolicy`, so the whole decision state machine builds and tests
in the `native` environment.

Input: the set of active conditions (a bitmask or small struct) + `nowMs`.
Output: a `WarningToken` enum (`NONE`, `OVERHEAT`, `FROST`, `SENSOR`,
`ACTUATOR`, `HUMID`) — enum order is priority order.

Internals: Schmitt margins for threshold conditions, the clear-dwell timer
(`WARNING_CLEAR_DWELL_S`, unsigned wrap-safe subtraction), and token change
detection. Threshold hysteresis lives here rather than in config: the config
stores one value per threshold, the policy adds the release margin — same
split as `safety_max_c` / `safety_hyst_c`… except simpler: margins are
compile-time constants, not configurable, because a mis-typed release margin is
not a user decision worth an NVS key.

*Alternative considered:* folding warning logic into `RefreshPolicy::evaluate()`
— rejected: the policy already tracks seven inputs; a separate decision with
its own state keeps both testable and the dwell timer out of the refresh
state machine.

### D2: Icon geometry — mirrored pair of fixed margin slots

```
left slot x 8..55, mirrored right slot x 145..192 (values stay centered on 0..199)
icon: filled triangle ~45 px tall, apex up, with a drawn '!' cutout bar
label: built-in 5x7 font, centered under the triangle, ≤ 8 chars
```

Both slots sit inside the partial window (y 30..199) and always show the same
token. Constants go alongside the existing layout constants in
`EPaperDisplay.cpp`. The triangle is drawn with `fillTriangle` + a white `!`
bar (fillRect), matching how the degree rings and control symbol are drawn
rather than printed.

An earlier revision of this design placed a single icon in the left margin and
rejected the right side because it "already carries setpoint/symbol/bar" — but
that content lives in the footer (y 152..), below the icon block (y 53..112),
so there is no collision. The mirrored pair was adopted for visibility from
either side of the room; the 5 px downward shift keeps the triangle visually
centred between the header band and the humidity line.

*Alternative considered:* right margin (mirrors the setpoint column) —
superseded by this decision.

### D3: Threshold config encoding

`DisplayConfig` gains:

- `float warn_frost_c = NAN` — `NAN` (or ≤ valid minimum) means disabled;
  NVS key `warn_frost` (10 chars)
- `uint8_t warn_humidity_pct = 0` — `0` means disabled; valid range
  1..100; NVS key `warn_hum` (8 chars)

`null` in the API maps to `NAN` in the struct. Validation falls back to
disabled for out-of-range stored values. Compile-time `nvsKeyFits` static
asserts, per house rule.

*Alternative considered:* a bool + value pair per threshold — rejected: two
NVS keys and two API fields per threshold double the surface for no
expressiveness gain; the disabled sentinel is unambiguous for both fields.

### D4: Warning onset bypasses the interval floor, clearance does not

Implemented in `DisplayManager::update()`: if the policy's token differs from
the displayed one and the new token is not `NONE`, refresh immediately;
otherwise let `RefreshPolicy` handle it as an ordinary change (token is passed
into `evaluate()`, which compares it like setpoint/demand). Onset urgency is
the whole point of a warning; a stale warning lingering one extra interval is
harmless and avoids pinning refreshes at the floor during flapping.

### D5: `render()` parameter — warning token, not a struct refactor

`render()` and `runPagedDraw()` gain one parameter (`WarningToken`). The
long-rumoured params-struct refactor is tempting, but it would touch the splash
and AP-info paths for no behavioural need and bloat the diff. Deferred; revisit
if `render()` grows past ~10 parameters.

### D6: Label strings live in a single table

`constexpr const char* const WARNING_LABELS[]` indexed by token, next to the
layout constants — one place guarantees label/priority/spec agreement. All
labels ≤ 8 chars, verified to fit the ~55 px slot at 6 px/char.

## Risks / Trade-offs

- **OVERHEAT onset latency is bounded by the control interval** (PID decimated
  to `control_interval_s`, default 60 s) — `isSafetyShutoffEngaged()` only
  updates on control ticks. The display warning is a second indicator, not a
  safety mechanism; the safety shutoff itself acts immediately at the hardware
  level. Document in the code, do not "fix" by polling the limit in the display
  path.
- **Single icon hides simultaneous conditions** → dropped conditions remain
  observable via `/api/status`, MQTT and syslog; priority order is fixed and
  spec'd. Rotation was rejected (would pin refreshes at the floor).
- **Partial-refresh frequency rises slightly** (warning onset bypasses the
  floor) → bounded by the dwell + hysteresis; onset is a rare event in normal
  operation. The brownout-risk observation will capture any effect.
- **`NAN` sentinel in NVS floats** — `Preferences::putFloat`/`getFloat` round-trip
  NaN payloads on ESP32; validation treats any non-finite value as disabled, so
  a corrupted read degrades to "off", which is the safe direction.
- **Stale warning on the glass after a crash** → first paint after every boot
  is a Full refresh with the splash, so a persisted image never outlives a
  reboot. No action needed.

## Migration Plan

Config keys are new; devices upgrade without migration. Defaults (both
thresholds disabled, warnings otherwise always evaluated) mean a device with
warnings configured sees at most the always-on conditions appear — acceptable
for a firmware update. Rollback is a normal firmware rollback; no NVS schema
change to undo.

## Open Questions

None — layout constants (exact triangle size within the 8..55 slot) are
implementation details to be tuned against a real panel during apply.
