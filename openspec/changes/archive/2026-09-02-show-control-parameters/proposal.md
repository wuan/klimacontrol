# Show control parameters in the web UI

## Why

The dashboard reports the controller as a single symbol. `control_active` is
`lastControlOutput > 0.0f`, so `●` means "demand is non-zero" — identical at 1 %
demand and at 100 %. Nothing else about the controller is visible anywhere: the
gains are `constexpr` inside an anonymous namespace in `SensorController.cpp`,
and the PID's accumulated integral and running state are private.

That is thin in three situations that all matter now:

- **There is no actuator.** Until the MQTT boolean output exists, the computed
  demand *is* the only observable result of the control loop. A dot cannot show
  whether the loop is behaving.
- **Tuning by hand.** The shipped gains are wrong for underfloor heating by
  orders of magnitude, and nobody can see what they currently are.
- **Judging autotune later.** `add-relay-autotuner-core` derives gains. Deciding
  whether a derived set is better than the current one needs both on screen.

## What Changes

- Expose the controller's live state and its gains:
  - `SensorController` gains accessors for the last control output, the PID
    gains, the integral accumulator, and whether the PID is running.
  - The gains move out of the anonymous namespace so they can be read. They stay
    compile-time constants — this change does not make them editable.
- **`GET /api/control`** returns the whole control picture in one response:
  enabled state, setpoint, current temperature, demand, integral, PID running
  flag, and the gains.
- **A collapsible "Control Parameters" panel** on the dashboard, following the
  existing "Show Measurements" idiom: fetched on demand, not on every poll.
- Demand is presented as a **percentage**, which is the honest reading of the
  controller's output and the same number a future actuator will consume.

## Non-goals

- **Making the gains editable.** That is the existing, still-unimplemented
  `temperature-control` §"PID parameter configurability" requirement: it needs
  `DeviceConfig` fields, NVS persistence, validation and a write endpoint. It is
  a prerequisite for autotune applying its results, and deserves its own change
  rather than being smuggled into a display feature.
- **Changing what `control_active` means.** Redefining it as valve state belongs
  with time-proportional output and the actuator.
- **Adding fields to `/api/status`.** Every client polls that endpoint on a
  timer; this is diagnostic detail that should be fetched only when someone is
  looking at it.
- Any autotune UI.

## Capabilities

### Modified Capabilities

- `http-api`: a new `GET /api/control` reporting the controller's live state and
  gains.
- `web-interface`: a collapsible control-parameters panel on the dashboard.

## Impact

- **Source**: `src/SensorController.h` (accessors), `src/SensorController.cpp`
  (gains out of the anonymous namespace), `src/routes/ControlRoutes.cpp` (the
  new GET).
- **Web**: `data/control.html`, and `src/generated/control_gz.h` regenerated.
- **Risk**: low. Read-only; no control-path behaviour changes.
