## 1. New control class

- [x] 1.1 Create `src/control/TemperatureController.h` declaring `Control::TemperatureController` with: ctor `(Config::ConfigManager&)`, `begin()`, `update(float temperature, bool valid, uint32_t nowMs)`, and every control-facing member moved from `SensorController.h` (`pid`, `autotuner`, `lastControlOutput`, `lastPidComputeMs`, `safetyShutoff`, `lastInputValid`, `actuatorAssigned`, `actuatorAgreement`, the three `std::atomic<bool>` request flags, `pendingGains`, `suspendPid()`, and all public accessors/request methods listed in the proposal). Carry the existing header comments over verbatim; change only the class name and the update signature.
- [x] 1.2 Create `src/control/TemperatureController.cpp` with `TAG = "control"` (same `#ifdef ARDUINO` idiom as `SensorController.cpp`). Move `setTargetTemperature`, `setControlEnabled`, `requestAutotuneStart`, `requestAutotuneCancel`, `acceptAutotuneResult`, `requestGains`, `isHeatingPermitted`, `suspendPid`, `updateControl` bodies verbatim. In `update()`, replace every `getTemperature()` with the `temperature` parameter, every `isDataValid()` with `valid`, and `millis()` with `nowMs`; set `lastInputValid = valid && !std::isnan(temperature)` at the top. `isHeatingPermitted()` becomes `enabled && !safetyShutoff && lastInputValid` per design D6.
- [x] 1.3 Move `begin()`'s PID-tuning block (the `pid.setGains(...)` from config + log line) into `TemperatureController::begin()`; move the `MinOutput`/`MaxOutput` anonymous-namespace constants and `getControlOutputMin()/Max()` statics.
- [x] 1.4 Add `+<control/TemperatureController.cpp>` to `build_src_filter` for `[env:native]` in `platformio.ini`.

## 2. Slim down SensorController

- [x] 2.1 Remove every control member, method, include (`control/PidController.h`, `control/RelayAutotuner.h`, `actuator/HeatingActuator.h`) and the control-related constructor initialisers from `SensorController.{h,cpp}`. Update the class comment so it describes only scheduling and the cache.
- [x] 2.2 Add `struct ProcessValue { float temperature = NAN; bool valid = false; uint32_t timestamp = 0; }` and `ProcessValue getProcessValue() const;` capturing all three fields under one `dataMutex` take, defaulting on timeout, with no vector copy. Reuse `Sensor::findMeasurement` for the temperature lookup.
- [x] 2.3 Confirm `pio test -e native -f test_sensor_scheduling -f test_memory_singleton_lifetimes -f test_sensor_controller_mutex_init` still passes unchanged.

## 3. Wire the second object through the firmware

- [x] 3.1 `main.cpp`: declare `Control::TemperatureController temperatureController(config);` directly after `statusLed`; in `setup()` call `temperatureController.begin()` after `sensorController.begin()` and move the `setTargetTemperature`/`setControlEnabled` calls onto it.
- [x] 3.2 `task/SensorMonitor.{h,cpp}`: ctor takes `(SensorController&, Control::TemperatureController&)`; loop body becomes `readSensors(startTime)` → `const auto pv = controller.getProcessValue();` → `control.update(pv.temperature, pv.valid, startTime);`. Keep the "called unconditionally" comment, re-pointed at `update()`.
- [x] 3.3 `Network.{h,cpp}`: add `Control::TemperatureController&` ctor parameter and member; actuator tick uses it for `getControlOutput()`, `isHeatingPermitted()`, `publishActuatorState()`; MQTT path keeps using `sensorController`.
- [x] 3.4 `WebServerManager.{h,cpp}`: add `Control::TemperatureController&` ctor parameter and member (forward-declare in the header).
- [x] 3.5 `routes/ControlRoutes.cpp`: retarget all control calls (including the two `getControlOutputMin/Max()` statics) to `temperatureController`; keep the single `sensorController.getTemperature()`.
- [x] 3.6 `routes/StatusRoutes.cpp`: retarget `getTargetTemperature`, `isControlEnabled`, `isControlActive`, `getReportedState`.
- [x] 3.7 `display/DisplayManager.{h,cpp}`: ctor takes `(SensorController&, Control::TemperatureController&)`; `update()` reads `getReportedState`, `getTargetTemperature`, `getControlOutput`, min/max from the control ref.
- [x] 3.8 Remove the now-unneeded `#include "SensorController.h"` from `routes/SettingsRoutes.cpp` if nothing else there uses it; add `control/TemperatureController.h` includes where needed.
- [x] 3.9 Build the firmware: `pio run -e adafruit_qtpy_esp32s2` compiles cleanly with no new warnings. This is the only build that compiles the route, Network and display call sites.

## 4. Retarget test_temperature_control onto the real class

- [x] 4.1 Delete `struct ControlLoop` and the `SHIPPED_GAINS` comment's reference to `SensorController.cpp`; leave the 20 pure-`PidController` cases untouched.
- [x] 4.2 Add a small fixture helper that builds `Config::ConfigManager` + `Control::TemperatureController`, enables control, sets setpoint 22 °C, `updateTuning(kp, ki, kd, intervalS)`, `updateActuatorTiming(cycleS, travelS, safetyMaxC, safetyHystC)`, then calls `begin()` so the gains are adopted.
- [x] 4.3 Port the eight stored-output/gating cases (`test_stored_output_*`, `test_control_disabled_returns_zero`, `test_nan_sensor_reading_returns_zero`, `test_loop_resumes_bumplessly_after_disabled_gap`) to `ctrl.update(temp, valid, nowMs)`, toggling enable via `config.updateTemperatureControlEnabled()`.
- [x] 4.4 Port the decimation cases (`test_pid_computes_once_per_control_interval`, `test_output_is_held_between_computations`, `test_integral_accumulates_across_the_interval`, `test_decimation_survives_millis_rollover`, `test_resumed_controller_computes_on_first_eligible_tick`, `test_one_second_interval_computes_every_tick`) using `control_interval_s` from config and `isControlRunning()`/`getControlIntegral()` for observation.
- [x] 4.5 Port the two safety cases (`test_safety_shutoff_not_delayed_by_control_interval`, `test_safety_shutoff_releases_on_a_sensor_tick`) against `isSafetyShutoffEngaged()` with limits from `updateActuatorTiming`.
- [x] 4.6 Rewrite `test_autotuner_ticks_every_sensor_cycle` against the real autotuner: `requestAutotuneStart()`, tick once with valid data, assert `isAutotuneActive()`, then assert `getAutotuneElapsedMs(now)` advances by 1000 on each 1 s tick with a 60 s control interval and that `isControlRunning()` stays false throughout.
- [x] 4.7 Add the four scenarios from the new "Control loop is decoupled from sensor acquisition" requirement: loop computes with no sensor object; `isHeatingPermitted()` false before the first tick; `isHeatingPermitted()` follows the last tick's `valid`; and the "Cadence is testable natively" scenario (exactly two computations across 121 ticks at 60 s interval — the spec originally said 61 ticks, but the shipped loop's zero baseline puts the first computation at 60 s, not on the first tick; spec scenario corrected to match).
- [x] 4.8 Run `pio test -e native -f test_temperature_control`. For any ported case that fails, decide per design risk 2: fix the test if the stand-in was wrong, or record a pre-existing bug in the task notes and leave the real code alone. — Result: all 16 ported cases and the 4 new ones pass against the real class unchanged; no pre-existing bug surfaced. Computations are observed as "loop running and output moved" (the real class has no counter), so the decimation cases use a 0.1 K error with `ki = 0.01` to keep successive outputs distinct.

## 5. Retarget test_pid_gain_requests

- [x] 5.1 Replace `SensorController controller(config, nullptr)` with `Control::TemperatureController controller(config)` and `updateControl()` with `update(NAN, false, now)`; update the header comment (the `millis()` caveat no longer applies).
- [x] 5.2 Add cadence cases now that the clock is injectable: gains requested at tick N are used by the computation at the next eligible tick; a gain change mid-interval suspends and the next computing tick restarts proportional-only (`getControlIntegral() == 0`).
- [x] 5.3 Run `pio test -e native -f test_pid_gain_requests`.

## 6. Verification and specs

- [x] 6.1 Full native suite: `pio test -e native` green.
- [x] 6.2 Firmware build green (repeat 3.9 after the test-driven fixes — done, clean) and, if hardware is available, flash and confirm (flashed and verified on device by the user afterwards): `GET /api/control` reports gains/output, the display footer shows the control symbol, the actuator tick logs under the `control` tag, and the control loop logs `control update` lines on the configured interval.
- [x] 6.3 `openspec validate split-control-loop-from-sensor-controller` passes; confirm the three delta specs (`temperature-control`, `sensor-management`, `system-architecture`) match the implemented method names.
- [x] 6.4 Grep `src/` and `test/` for `updateControl(` and `SensorController::getControl` to confirm no stale call sites or comments remain.
