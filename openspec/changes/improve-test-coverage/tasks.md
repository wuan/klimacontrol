## 1. Temperature Control Tests

- [x] 1.1 Create test_temperature_control directory structure
- [x] 1.2 Implement PID calculation tests (Kp, Ki, Kd terms)
- [x] 1.3 Add derivative guard against dt=0 test
- [x] 1.4 Add setpoint change tests
- [x] 1.5 Add control enable/disable tests
- [x] 1.6 Add edge case tests (NaN sensor reading, output saturation)
- [x] 1.7 Verify all tests pass with `pio test -e native -f test_temperature_control`

## 2. SHT4x Sensor Tests

- [x] 2.1 Create test_sht4x directory structure
- [x] 2.2 Implement mock SHT4x with controllable read() behavior
- [x] 2.3 Test SHT4x providesMeasurements() returns correct types
- [x] 2.4 Test SHT4x read() returns valid temperature/humidity measurements
- [x] 2.5 Test SHT4x handles read failure gracefully
- [x] 2.6 Verify all tests pass with `pio test -e native -f test_sht4x`

## 3. SensorController Expansion

- [x] 3.1 Add three-sensor averaging test
- [x] 3.2 Add invalid readings exclusion from average test
- [x] 3.3 Add snapshot consistency test
- [x] 3.4 Add getValidMeasurements() returns empty on invalid data test
- [x] 3.5 Verify all tests pass with `pio test -e native -f test_sensor_controller`

## 4. Config Tests Expansion

- [ ] 4.1 Add missing NVS key handling test
- [ ] 4.2 Add corrupted data handling test
- [ ] 4.3 Add factory reset behavior test
- [ ] 4.4 Verify all tests pass with `pio test -e native -f test_config`

## 5. Network Helpers Tests

- [x] 5.1 Create test_network_helpers directory structure
- [x] 5.2 Test WiFi reconnection counter threshold logic
- [x] 5.3 Test NTP epoch guard conditions (both zero, elapsed time, underflow protection)
- [x] 5.4 Test mDNS hostname generation from MAC address
- [x] 5.5 Verify all tests pass with `pio test -e native -f test_network_helpers`

## 6. OTAUpdater Tests

- [x] 6.1 Create test_ota_updater directory structure
- [x] 6.2 Test version comparison logic
- [x] 6.3 Test hasEnoughMemory() boundary conditions
- [x] 6.4 Verify all tests pass with `pio test -e native -f test_ota_updater`

## 7. Final Verification

- [x] 7.1 Run full test suite: `pio test -e native`
- [x] 7.2 Verify firmware builds: `pio run -e adafruit_qtpy_esp32s2`