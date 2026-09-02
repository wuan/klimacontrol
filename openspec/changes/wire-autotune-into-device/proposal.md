# Wire the relay autotuner into the device

## Why

`add-relay-autotuner-core` built and proved `Control::RelayAutotuner`, but
nothing references it: no run can be started, and the component cannot be
exercised on real hardware at all. This change connects it — control-loop
ownership, HTTP endpoints, and a UI — so the plumbing exists and can be
exercised end to end.

## Known limitation, accepted deliberately

**There is still no heating output.** The controller's demand is computed and
displayed but drives nothing physical, so during a run the relay's on/off
command does not change the room temperature. The plant therefore does not
respond to the experiment, and every run will end in `SettlingTimeout` or
`RunTimeout` rather than converging.

This was raised before starting and the decision was to build the wiring anyway.
It is a reasonable thing to want: the state machine, the task handoff, the
endpoints and the UI can all be verified now, so that when the MQTT boolean
actuator lands the only new thing is the output itself.

The obligation this creates is that the UI must not present a guaranteed timeout
as a malfunction. It states up front that runs cannot converge until an actuator
exists, and it reports the abort reason plainly.

## What Changes

- `SensorController` owns a `RelayAutotuner` and gives it priority over the PID
  while a run is active. The PID is suspended for the duration, so it resumes
  bumplessly afterwards through the machinery already built.
- Start and cancel are **requests**, not direct calls. The web task sets an
  atomic flag; the Sensor Monitor task consumes it inside `updateControl()`.
  This is the same rule already established for the PID accumulators — only the
  control-loop task touches control state — and it is why `setControlEnabled()`
  is a pure config write.
- `POST /api/autotune/start`, `/abort`, `/accept` and `GET /api/autotune/status`.
- Autotune status and controls in the existing control-parameters panel.
- `PidController::setGains()`, which suspends, so accepting a result is treated
  as the discontinuity it is.

## Non-goals

- **Persisting accepted gains.** `DeviceConfig` has no gain fields; that is the
  separate, still-unimplemented "PID parameter configurability" requirement.
  Accepting a result applies it **in memory only**, and it is lost on reboot.
  The UI says so rather than implying otherwise.
- **A reboot marker for interrupted runs.** Run state lives in RAM, so a restart
  silently returns the autotuner to `Idle` rather than reporting an interrupted
  run. Recording that needs the same NVS plumbing as above.
- Any heating output.

## Capabilities

### Modified Capabilities

- `pid-autotune`: control-loop ownership, the task handoff, and in-memory
  application of a result.
- `http-api`: the four autotune endpoints.
- `web-interface`: autotune status and controls.

## Impact

- **Source**: `src/SensorController.{h,cpp}`, `src/control/PidController.h`,
  `src/routes/ControlRoutes.cpp`.
- **Web**: `data/control.html` and the regenerated header.
- **Risk**: a run takes over the control output. With no actuator that is
  inert, but the ownership rule and the abort paths still need to be right
  before an actuator exists.
