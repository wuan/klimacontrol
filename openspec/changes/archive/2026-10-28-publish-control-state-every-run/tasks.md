# Tasks

## 1. Control-loop computation counter

- [x] 1.1 Add a `std::atomic<uint32_t> controlComputeCount` to `Control::TemperatureController` with a `getControlComputeCount()` accessor, and increment it at the PID compute point in `update()` (the branch past the decimation gate that calls `pid.update()`). Verify `pio run -e adafruit_qtpy_esp32s2` compiles and `pio test -e native -f test_temperature_control` still passes.
- [x] 1.2 Add native test cases in `test/test_temperature_control/test_temperature_control.cpp` asserting the counter advances exactly once per control interval, does not advance on a decimated tick, and does not advance on a disabled, no-valid-data or shutoff tick. Verify with `pio test -e native -f test_temperature_control`.

## 2. MQTT republish on each computation

- [x] 2.1 Add a `lastControlComputeCount` member to `Net::MqttPublisher`, seed it when a (re)connect is detected, and in `tick()` call `publishControlState(true)` whenever the controller's counter differs from the stored value. Verify `pio run -e adafruit_qtpy_esp32s2` compiles.
- [x] 2.2 Update the MQTT "Topics" note in `README.md` (the paragraph after the topic table, ~lines 293-295) to state that the control-state set is republished on every control-loop computation. Verify the wording matches the implemented cadence.

## 3. Integration verification

- [x] 3.1 Run the full native suite with `pio test -e native` and confirm every test passes.
- [x] 3.2 On hardware with MQTT enabled and a 60 s control interval, subscribe with `mosquitto_sub -t 'sensors/#' -v` and confirm `target_temperature`, `control_output` and `heating` are republished roughly every 60 s even when the setpoint is unchanged, and once on connect.

## Workflow follow-up

- Validate the change with `openspec validate --all --strict`.
- Archive the change after review.
