## Context

The project uses PlatformIO with a `native` test environment for unit tests via Unity framework. Existing tests cover Sensor base class, StatusLed, and some SensorController helper logic, but critical components lack coverage.

Current test status:
- `test_sensor_base`: Comprehensive (16 tests) - covers Sensor base class state machine
- `test_status_led`: Good (11 tests) - covers LED state transitions
- `test_sensor_controller`: Some helper tests but missing core control logic
- `test_sensor_utils`: Utility function tests
- `test_config`: Limited NVS persistence tests
- `test_mqtt_client`: Basic structure tests
- `test_support`: Support utilities tests

## Goals / Non-Goals

**Goals:**
- Add focused unit tests for temperature control PID algorithm
- Add tests for SHT4x sensor reading logic
- Expand Config NVS persistence coverage
- Test Network helper logic (WiFi reconnection, NTP sync)
- Test SensorController multi-sensor averaging and validation
- Maintain test patterns consistent with existing test suite

**Non-Goals:**
- Testing hardware-specific code (requires ESP32 hardware)
- Integration tests with actual sensors (requires physical hardware)
- Testing web routes (require Arduino build)
- Modifying production code to accommodate testing

## Decisions

1. **New `test_temperature_control` suite** - Create dedicated test directory for PID algorithm tests
   - Test PID calculation, setpoint changes, enable/disable behavior
   - Mirror the pattern used in `test_sensor_controller` where core algorithms are extracted into testable helper functions

2. **New `test_sht4x` suite** - Create dedicated test directory for SHT4x sensor
   - Test SHT4x::read() with mock I2C responses
   - Test temperature/humidity/dewpoint calculation

3. **Expand `test_sensor_controller`** - Add tests for averaging logic
   - Multi-sensor averaging scenarios
   - Invalid data handling
   - Snapshot consistency

4. **Expand `test_config`** - Add NVS persistence edge cases
   - Missing keys, corrupted data, factory reset behavior

5. **New `test_network_helpers` suite** - Test WiFi/NTP logic in isolation
   - WiFi reconnection counter logic
   - NTP epoch guard conditions
   - mDNS hostname generation

## Risks / Trade-offs

[Risk] Tests may become tightly coupled to implementation details
→ Mitigation: Focus on observable behavior and contract, not internal state

[Risk] Mock sensors may drift from actual sensor behavior
→ Mitigation: Keep mocks minimal and well-documented; test actual algorithm logic

[Risk] New tests may slow down CI
→ Mitigation: Tests are lightweight unit tests; native environment runs quickly