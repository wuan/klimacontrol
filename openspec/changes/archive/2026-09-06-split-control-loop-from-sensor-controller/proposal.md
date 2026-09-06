## Why

`SensorController` is two objects under one name: a sensor scheduler/cache (sensors, per-sensor slots, the mutex-guarded measurement snapshot) and the heating control loop (PID, relay autotuner, over-temperature shutoff, actuator-state publication, cross-task request handover). The only seam between them is that the control half calls `getTemperature()` and `isDataValid()`. That coupling has a concrete cost today: the 36 control-loop tests in `test/test_temperature_control` cannot construct the real loop, so they run against a hand-written `struct ControlLoop` stand-in that has already drifted from the shipped code (its autotune branch does not call the autotuner and it has no gain-change handover), and `test/test_pid_gain_requests` cannot assert on cadence because `updateControl()` reads `millis()` internally.

## What Changes

- Add `Control::TemperatureController` in `src/control/`, holding everything control-related that `SensorController` holds today: `PidController`, `RelayAutotuner`, the shutoff latch, `lastControlOutput`, decimation timestamp, actuator-state fields, the `autotuneStart/Cancel` and `gainsChangeRequested` atomics, and all control-facing accessors and request methods.
- The loop's process value is **pushed in** by the Sensor Monitor task: `update(float temperature, bool valid, uint32_t nowMs)`. The control class holds no reference to `SensorController`. The clock is a parameter, as it already is for `PidController` and `RelayAutotuner`.
- `SensorController` gains one accessor, `getProcessValue()`, returning `{temperature, valid, timestamp}` as a small struct captured under a single lock take, so the task hands the loop a consistent pair instead of the loop taking the mutex four times per tick.
- **BREAKING (internal API)**: `SensorController` loses every control method (`setTargetTemperature`, `setControlEnabled`, `isControlEnabled`, `isControlActive`, `getReportedState`, `publishActuatorState`, `getControl*`, `request*`, `acceptAutotuneResult`, `getAutotune*`, `isAutotuneActive`, `isSafetyShutoffEngaged`, `isHeatingPermitted`, `updateControl`). Callers in `Network.cpp`, `routes/ControlRoutes.cpp`, `routes/StatusRoutes.cpp`, `display/DisplayManager.cpp` and `main.cpp` move to the new class.
- `isHeatingPermitted()` is answered from the inputs of the most recent control tick rather than a live sensor read, so it is at most one Sensor Monitor tick (1 s) stale. The safety shutoff already latches on invalid input within the same tick, so the actuator's behaviour does not change.
- Control log lines move from the `sensor` tag to a `control` tag.
- A second file-scope global in `main.cpp`; `SensorMonitor`, `Network`, `WebServerManager` and `DisplayManager` take a `Control::TemperatureController&` in addition to (or, for the display, instead of parts of) the `SensorController&` they hold now.
- Tests: the `struct ControlLoop` stand-in in `test/test_temperature_control` is deleted and its cases drive the real `TemperatureController`, with setpoint, enable flag and safety limits configured through `Config::ConfigManager`. `test/test_pid_gain_requests` moves onto the new class and gains cadence/decimation assertions. `src/control/TemperatureController.cpp` is added to the native `build_src_filter`.

## Capabilities

### New Capabilities

_None._ The control loop is an existing capability (`temperature-control`, `pid-autotune`); this change moves where it lives and how it receives its input.

### Modified Capabilities

- `temperature-control`: the PID's process variable is supplied by the Sensor Monitor task as a parameter of the control update, not read from `SensorController::getTemperature()`; the control update is invoked on every sensor tick with `valid = false` rather than being skipped when no data is available (the current text contradicts itself on this and is reconciled); `setTargetTemperature()` clamping moves to the new class; `isHeatingPermitted()` staleness bound is stated.
- `sensor-management`: the mandated `SensorController` API surface is pruned to sensor concerns and gains the single-lock `getProcessValue()` accessor.
- `system-architecture`: the component inventory names `Control::TemperatureController` as the owner of control-loop state and states the single-writer rule for it (written only by the Sensor Monitor task; other tasks read scalars or post requests).

`pid-autotune` is not modified: its requirements describe the autotuner and the request/accept handover without naming the host class, and all of them continue to hold.

## Impact

- **Source**: new `src/control/TemperatureController.{h,cpp}`; `src/SensorController.{h,cpp}` shrinks by roughly half; call-site edits in `src/task/SensorMonitor.{h,cpp}`, `src/Network.{h,cpp}`, `src/WebServerManager.{h,cpp}`, `src/routes/ControlRoutes.cpp`, `src/routes/StatusRoutes.cpp`, `src/display/DisplayManager.{h,cpp}`, `src/main.cpp`.
- **HTTP API**: no wire change. Every endpoint keeps its shape; only the object the handlers call into changes.
- **Actuator**: `HeatingActuator` and `ShellyChannel` are untouched; they never referenced `SensorController`.
- **Tests**: `test/test_temperature_control` (stand-in removed, cases re-targeted), `test/test_pid_gain_requests` (re-targeted, extended), `platformio.ini` native filter. `test_sensor_scheduling`, `test_memory_singleton_lifetimes` and `test_sensor_controller_mutex_init` exercise only the sensor half and are unaffected.
- **Runtime**: one mutex take per tick for the process value instead of four; a few dozen bytes more BSS for the second global; no heap change. Log consumers see control lines under a new tag; the in-progress `add-heating-cutover-runbook` change was checked and does not depend on log tags.
- **Specs**: delta specs for `temperature-control`, `sensor-management`, `system-architecture`.
