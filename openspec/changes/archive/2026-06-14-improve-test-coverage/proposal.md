## Why

The project has a good foundation of unit tests but test coverage is inconsistent across components. Some areas like the Sensor base class and StatusLed have solid coverage, while critical functionality like temperature control (PID algorithm), configuration persistence, and sensor implementations lack dedicated tests. This limits confidence in making changes and risks regressions.

## What Changes

- Add comprehensive tests for temperature control (PID algorithm, setpoint changes, enable/disable)
- Add tests for SHT4x sensor implementation
- Expand Config tests to cover NVS persistence scenarios
- Add tests for Network connectivity helpers (WiFi reconnection, NTP sync logic)
- Add tests for OTAUpdater utility functions
- Add integration-style tests for SensorController averaging and data validation
- Establish consistent test patterns across all test suites

## Capabilities

### New Capabilities
- `test-temperature-control`: Tests for the temperature control system including PID calculations, setpoint changes, control enable/disable, and edge cases

### Modified Capabilities
- `sensor-management`: Expand sensor tests to cover SHT4x implementation and multi-sensor averaging scenarios

## Impact

- New test files: `test/test_temperature_control/`, `test/test_sht4x/`
- Modified test files: `test/test_sensor_controller/`, `test/test_config/`, `test/test_network_helpers/`
- Test infrastructure remains unchanged (Unity test framework, PlatformIO native environment)