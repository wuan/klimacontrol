# Add web UI for enabling control and adjusting the setpoint

## Why

The control panel shows the target temperature and the control state, but there
is no way to change either from a browser. `data/control.html:49-58` renders
both as read-only text, and nothing in `data/` ever POSTs to the three control
endpoints. Adjusting the setpoint currently requires a `curl` call.

The firmware side is already complete and has been for some time:

| Endpoint | Handler | Persists |
|---|---|---|
| `POST /api/temperature/target` | `ControlRoutes.cpp:14` | `Config::updateTargetTemperature()` → NVS |
| `POST /api/control/enable` | `ControlRoutes.cpp:47` | `Config::updateTemperatureControlEnabled()` → NVS |
| `POST /api/control/disable` | `ControlRoutes.cpp:57` | same |

`/api/status` already reports `target_temperature`, `control_enabled` and
`control_active` (`StatusRoutes.cpp:52-54`), and the e-paper `RefreshPolicy`
already re-renders on setpoint or control-state change — its comment at
`RefreshPolicy.cpp:102` explicitly anticipates a user who "can change [these]
from the web UI at any time". Only the UI is missing.

This is therefore not a new capability but an **unclosed spec gap**. Two
existing requirements were written and never implemented:

- `web-interface` §"Temperature control UI" already mandates a 0.5 °C stepper
  and an enable/disable toggle on the dashboard.
- `http-api` §"Out-of-range setpoint" already mandates HTTP 4xx with the
  setpoint unchanged. The implementation silently clamps to `[10, 30]` and
  answers `{"success":true}`, so a client that asks for 45 °C is told it
  succeeded while the device stored 30.

Building the stepper makes that divergence user-visible: a stepper parked at the
30 °C rail would keep incrementing on screen, and only snap back on the next
poll. Fixing the endpoint is cheaper than building UI to paper over it.

### The controller does not restart cleanly

Making the enable toggle reachable from a browser turns a latent control-loop
defect into an everyday one. `updateControl()` returns early when control is
disabled (`SensorController.cpp:444-450`) **without updating `lastControlTime`**.
On the first tick after re-enabling:

```
dt       = (now - lastControlTime) / 1000    ← the whole disabled duration
integral += Ki * error * dt                  ← saturates immediately
```

Disable for an hour, re-enable with the room 0.1 °C off target, and `dt = 3600`.
The integral is slammed to the anti-windup clamp on the first tick and the
output goes to `MaxOutput` irrespective of how small the error is. Zeroing
`integral` alone would not help — the next statement re-saturates it from the
stale `dt`. All three of `integral`, `previousError` and `lastControlTime` have
to be reseated together.

The same early return also fires on `!isDataValid() || isnan(currentTemp)`, so a
five-minute sensor dropout saturates the integral on recovery with no user
involved. Scoping the fix to "on enable" would fix one of three cases; scoping
it to "the loop did not run on the previous tick" fixes enable, sensor recovery
and first-tick-after-boot with the same code. That is the correct framing: it is
a *bumpless restart*, not an enable handler.

## What Changes

- **Dashboard stepper and toggle** (`data/control.html`). A `− 21.5° +` stepper
  in the existing `.control-bar`, stepping 0.5 °C and clamped client-side to
  `[10.0, 30.0]`; a toggle switch for enable/disable reusing the existing
  `.toggle-row` / `.toggle-switch` styles from `common.css`. The setpoint stays
  adjustable while control is disabled, so a target can be dialled in before
  switching on.
- **Poll/edit arbitration.** `updateStatus()` currently overwrites the setpoint
  unconditionally every 10 s. It gains a guard so a poll in flight cannot revert
  a value the user is mid-way through adjusting.
- **Reject out-of-range setpoints** (`ControlRoutes.cpp`) with HTTP 400 and no
  state change, as `http-api` already specifies, replacing the silent clamp.
  Reconcile the two disagreeing validators: `SensorController::setTargetTemperature()`
  clamps to `[10, 30]` while `Config::updateTargetTemperature()` resets to
  `22.0`, a fallback currently unreachable through this path.
- **Bumpless controller restart** (`SensorController`). Promote the three
  function-local `static`s in `updateControl()` to instance members and reseat
  them whenever the loop resumes after a tick in which it did not run.

## Non-goals

- Changing the `/enable` + `/disable` endpoint pair into a single
  `{"enabled": bool}` endpoint. The split shape is slightly awkward for a
  toggle, but it is specified, implemented and has external callers.
- Changing the dashboard polling cadence. `web-interface` §"Polled live updates"
  specifies 2 s; `control.html:174` polls at 10 s. That divergence is real and
  predates this change — recorded here so it is not lost, but reconciling it is
  separate work, and a faster poll would make the poll/edit race sharper rather
  than milder.
- Any re-tuning of `Kp`/`Ki`/`Kd`.
- Presenting the setpoint anywhere other than the control page.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `web-interface`: the dashboard temperature-control UI gains concrete
  behaviour for the stepper rails, the disabled-control case, and the
  interaction between polling and in-progress edits.
- `http-api`: `POST /api/temperature/target` rejects out-of-range values rather
  than clamping them.
- `temperature-control`: setpoint validation becomes rejection rather than
  clamping, and a new requirement covers bumpless restart of the PID state.

## Impact

- **Web assets**: `data/control.html`; `src/generated/control_gz.h` regenerated
  via `scripts/compress_web.py`.
- **Source**: `src/routes/ControlRoutes.cpp`, `src/SensorController.h`,
  `src/SensorController.cpp`, possibly `src/Config.cpp`.
- **Tests**: `test/test_temperature_control/test_temperature_control.cpp` holds
  a hand-copied reimplementation of `updateControl()` (line 204) that takes
  `nowMs` as a parameter to escape `millis()`. It must be updated in lockstep or
  the suite validates a fiction. See `design.md` for whether to extract the PID
  instead of continuing to mirror it.
- **Hardware**: none. Rapid setpoint changes cannot outpace the e-paper panel —
  `RefreshPolicy`'s minimum-interval floor (`RefreshPolicy.cpp:116`) already
  bounds refresh rate, which keeps this clear of the open
  `assess-display-brownout-risk` question.