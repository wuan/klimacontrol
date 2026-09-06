## Context

`SensorController` (`src/SensorController.{h,cpp}`, ~1240 lines) holds two unrelated bodies of state:

```
SensorController
┌──────────────────────────────────┐   ┌──────────────────────────────────────┐
│ SENSOR HALF                      │   │ CONTROL HALF                         │
│  sensors[], slots[]              │   │  pid, autotuner                      │
│  currentMeasurements, dataMutex  │   │  lastControlOutput, lastPidComputeMs │
│  readSensors(now)                │──▶│  safetyShutoff                       │
│  getSnapshot/getMeasurements     │ T │  actuatorAssigned/Agreement          │
│  getTemperature/Humidity/...     │ + │  atomics: autotuneStart/Cancel,      │
│  reserveSensorSlots, addSensor   │valid          gainsChangeRequested      │
│  sortSensors, I2C recovery       │   │  updateControl()                     │
└──────────────────────────────────┘   └──────────────────────────────────────┘
   written by SensorMonitor task            written by SensorMonitor task
   read by all, under dataMutex             read by web/Network/display as
                                            lock-free scalars; requests via atomics
```

The only dependency runs right-to-left: `updateControl()` calls `getTemperature()` and `isDataValid()`, each taking `dataMutex` separately, four times per tick. Nothing in the sensor half reads control state.

Callers today (from the caller map done during exploration):

| Caller | Sensor half | Control half |
|---|---|---|
| `task/SensorMonitor.cpp` | `readSensors` | `updateControl` |
| `main.cpp` | reserve, addSensor, begin | setTarget, setEnabled |
| `Network.cpp` | isDataValid, getValidMeasurements (MQTT) | getControlOutput, isHeatingPermitted, publishActuatorState |
| `routes/ControlRoutes.cpp` | one `getTemperature` | ~25 sites: whole autotune + gains API |
| `routes/StatusRoutes.cpp` | 12 sites | 4 sites |
| `routes/SensorRoutes.cpp` | 3 sites | none |
| `display/DisplayManager.cpp` | getSnapshot | getReportedState, getTargetTemperature, getControlOutput, min/max |

`actuator/*` never references `SensorController`.

Test state: `test/test_temperature_control` (36 cases) drives a local `struct ControlLoop` re-implementation because the real loop needs a live sensor to produce a temperature. The stand-in already diverges: its autotune branch only counts ticks, and it has no gain-change handover. `test/test_pid_gain_requests` (8 cases) drives the real class but, because `updateControl()` reads `millis()`, can only assert on what a single tick does.

Constraints that must survive the split:

- Control state is **single-writer**: only the Sensor Monitor task mutates PID/autotuner/shutoff state. Other tasks read plain scalars (safe on the single-core S2) or post requests through `std::atomic<bool>` flags consumed with `exchange()`.
- The loop is invoked on **every** sensor tick, including when disabled or without valid data, so it can mark skipped ticks for bumpless restart. Only the PID computation is decimated.
- Both objects are file-scope globals in `main.cpp`, constructed before `config.begin()` has read NVS, hence the `begin()` step that applies stored tuning.
- Native build (`pio test -e native`) compiles no route handlers and no task loop; everything the tests need must be reachable without `ARDUINO`.

## Goals / Non-Goals

**Goals:**

- One class per concern: `SensorController` schedules and caches sensors; `Control::TemperatureController` runs the loop.
- The control class has **no dependency on sensors**. Its inputs are parameters.
- The clock is injectable end-to-end, matching `PidController` and `RelayAutotuner`.
- The 36 control-loop tests and the 8 gain-request tests run against the real class; the stand-in is deleted.
- Fewer lock takes per tick (four to one), with temperature and validity read as one consistent pair.
- Zero HTTP API change, zero actuator change, zero persisted-data change.

**Non-Goals:**

- Changing any control behaviour. Code moves verbatim wherever possible; the tests are what prove nothing drifted.
- Touching `HeatingActuator`, `ShellyChannel`, `PidController`, `RelayAutotuner`.
- Cleaning up the duplicate `lastReadingTime` / `lastReadingTimestamp` in the sensor half.
- Renaming `SensorController` or splitting it further.
- Reducing the Sensor Monitor task's role; it still owns both objects' write side.

## Decisions

### D1. Inputs are pushed by the task; the control class holds no `SensorController&`

```
SensorMonitor::task(), once per second:
    controller.readSensors(now);
    const auto pv = controller.getProcessValue();           // one lock take
    control.update(pv.temperature, pv.valid, now);
```

`Control::TemperatureController::update(float temperature, bool valid, uint32_t nowMs)` replaces `updateControl()`.

*Alternative considered*: the control class keeps a `SensorController&` and calls `getTemperature()`. Smallest diff, but it leaves the test problem exactly where it is (a real sensor is still needed to feed the loop), keeps the four-lock TOCTOU, and keeps the clock hidden. Rejected.

*Why not pass a `Snapshot`*: `getSnapshot()` copies a `std::vector<Measurement>`, i.e. a heap allocation per second on a device that tracks fragmentation. The loop needs two scalars.

### D2. `SensorController::getProcessValue()` returns a three-field struct under one lock

```cpp
struct ProcessValue {
    float    temperature = NAN;
    bool     valid       = false;
    uint32_t timestamp   = 0;    // lastReadingTimestamp
};
ProcessValue getProcessValue() const;
```

Same lock discipline as `getSnapshot()`: on mutex timeout it returns the default (invalid, NaN), which the loop already treats as "no data". `valid` mirrors `dataValid`; `temperature` is the first `Temperature` measurement or NaN, exactly what `getTemperature()` returns today.

### D3. Name, namespace, location: `Control::TemperatureController` in `src/control/`

Sits next to `PidController`, `RelayAutotuner`, `TimeProportionalOutput`, which it composes. The spec capability is already called `temperature-control`. `HeatingController` was considered (the actuator is one-directional) and rejected for consistency with the spec name; `ControlLoop` was rejected because it collides with the test stand-in being deleted.

### D4. Config access stays as it is: the class holds `Config::ConfigManager&`

Setpoint, enable flag, gains, control interval, safety limit and hysteresis all live in `DeviceConfig`. `update()` keeps taking one `getDeviceConfigSnapshot()` at the top of the tick (the existing torn-read defence). `setTargetTemperature()` and `setControlEnabled()` remain **configuration writes only**, as the spec requires, and move over unchanged. The constructor reads the compiled-in defaults from the config cache; `begin()` applies NVS tuning, called from `setup()` before the task exists.

### D5. Cross-task contract is preserved verbatim

Moves with the class, unchanged:

- `autotuneStartRequested`, `autotuneCancelRequested`, `gainsChangeRequested` + `pendingGains` with the release/acquire pattern.
- `publishActuatorState()` written by the Network task; `actuatorAssigned` / `actuatorAgreement` read by API and display.
- Lock-free scalar getters (`getControlOutput`, `getControlGains`, `getControlIntegral`, `isControlRunning`, autotune getters), documented as single 32-bit reads of single-writer members on a single-core part.
- `getControlOutputMin()/Max()` statics.

### D6. `isHeatingPermitted()` answers from the last tick's inputs

Today it reads `isDataValid()` and `getTemperature()` live from the Network task. With no sensor reference, `update()` records `lastInputValid = valid && !isnan(temperature)` and `isHeatingPermitted()` becomes:

```
enabled (config snapshot) && !safetyShutoff && lastInputValid
```

Semantics: at most one Sensor Monitor tick (1 s) stale; `false` before the first tick, which is the safe default. The over-temperature shutoff already engages on the same tick that sees invalid input, so the actuator's observable behaviour is unchanged. Stated in the `temperature-control` delta spec.

### D7. Log tag `control`

Control lines currently log under `sensor`. The new translation unit gets `static const char* TAG = "control"` with the same `#ifdef ARDUINO` / `#define TAG` dance as `SensorController.cpp`. The in-progress `add-heating-cutover-runbook` change was checked and does not grep log tags.

### D8. Wiring

- `main.cpp`: `Control::TemperatureController temperatureController(config);` declared after `config`, before `sensorMonitor`. `setup()` calls `temperatureController.begin()` after `sensorController.begin()` and moves `setTargetTemperature`/`setControlEnabled` onto it.
- `Task::SensorMonitor(SensorController&, Control::TemperatureController&)`.
- `Network(config, sensorController, temperatureController, sensorMonitor, statusLed, webServer)`. Uses the control ref for the actuator tick, the sensor ref for MQTT.
- `WebServerManager(config, network, sensorController, temperatureController, sensorMonitor)`. Both members available to `routes/*.cpp`; `ControlRoutes.cpp` still needs `sensorController.getTemperature()` for one field.
- `Display::DisplayManager(SensorController&, Control::TemperatureController&)`.

### D9. Test strategy

**`test_temperature_control`**: delete `struct ControlLoop`. The 16 gating/decimation cases (`test_stored_output_*` through `test_one_second_interval_computes_every_tick`) become fixture-based:

```cpp
Config::ConfigManager config;
Control::TemperatureController ctrl(config);
config.updateTemperatureControlEnabled(true);
config.updateTargetTemperature(22.0f);
config.updateTuning(kp, ki, kd, /*intervalS=*/1);   // then ctrl.begin() to adopt
config.updateActuatorTiming(..., safetyMaxC, safetyHystC, ...);
ctrl.update(18.0f, true, 1000);
```

`ConfigManager` updaters work on native today (`test_pid_gain_requests` already relies on `updateTuning`). The one case that stubbed autotune with a plain flag (`test_autotuner_ticks_every_sensor_cycle`) is rewritten against the real autotuner: `requestAutotuneStart()`, tick with valid data, assert `isAutotuneActive()` and that `getAutotuneElapsedMs()` advances on every 1 s tick with a 60 s control interval. The 20 pure-`PidController` cases at the top of the file are untouched.

**`test_pid_gain_requests`**: retarget to `Control::TemperatureController`; replace `updateControl()` with `update(NAN, false, now)` (a tick with no data still applies the pending gains, as the header comment already relies on). Add cases that were impossible before: gains applied on tick N are used by the computation on tick N + interval; a gain change mid-interval suspends and the next eligible tick restarts proportional-only.

**Build**: add `+<control/TemperatureController.cpp>` to `build_src_filter` in `platformio.ini`. Firmware build (`pio run -e adafruit_qtpy_esp32s2`) is the only thing that compiles the route/Network/display call sites, so it is a required verification step, not optional.

## Risks / Trade-offs

- [Behaviour drift while moving ~300 lines] → Move `updateControl()` and friends verbatim; only the two sensor calls become parameter reads. The 44 re-targeted tests are the regression net, and they are stricter than before because they now hit real code.
- [Tests that passed against the stand-in fail against the real class] → That is a finding, not a blocker. Triage each: if the real code is right and the stand-in was wrong, fix the test's expectation and note it in the task; if the real code is wrong, it is a pre-existing bug and gets its own follow-up rather than being fixed silently inside a refactor.
- [`isHeatingPermitted()` one tick stale] → Bounded at 1 s by the Sensor Monitor cadence; actuator tick is coarser (`HeatingActuator::TICK_MS`); shutoff latch already covers invalid input on the same tick. Documented in spec.
- [Native tests do not compile the call sites] → Firmware build is a mandatory task; `WebServerManager`/`Network`/`DisplayManager` constructor changes are only proven there.
- [Global construction order in `main.cpp`] → New global depends only on `config`, which is declared first. Declare it immediately after `statusLed`.
- [Second copy of the `TAG` pattern] → Same three-line idiom as every other `.cpp`; not worth a shared header in this change.
- [Log tag change surprises someone grepping `sensor`] → Called out in proposal; runbook checked.

## Migration Plan

Single change, no persisted-data or wire format impact. Deploy as a normal firmware build; roll back by reverting the commit. No NVS migration; `DeviceConfig` is untouched.

## Open Questions

None blocking. One judgement call is left to implementation: whether the `ProcessValue.timestamp` field is used by the loop at all (today the loop does not read the reading timestamp). It is included because it is free under the same lock and useful for a future "stale reading" guard; drop it if it ends up unused.
