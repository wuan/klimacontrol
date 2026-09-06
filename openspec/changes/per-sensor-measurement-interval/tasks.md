## 1. Sensor interface

- [x] 1.1 Add `[[nodiscard]] virtual uint32_t requiredIntervalMs() const { return 0; }` to `Sensor::Sensor` in `src/sensor/Sensor.h` with a doc comment explaining the 0 / non-zero semantics
- [x] 1.2 Override `requiredIntervalMs()` in `src/sensor/SGP40.h` to return `1000`, with a comment citing the Sensirion VOC index 1 Hz sampling assumption
- [x] 1.3 Remove the private `co2` member from `src/sensor/SCD4x.h` and change `SCD4x::read()` in `src/sensor/SCD4x.cpp` to return `valid = false` with no measurements when data is not ready or the read fails (D7)

## 2. SensorController cache and scheduling

- [x] 2.1 Add `static constexpr uint32_t MEASUREMENT_INTERVAL_MS = 15000;` as a public member of `SensorController` (D9)
- [x] 2.2 Add a private `SensorSlot` struct (`measurements`, `lastValidMs`, `lastReadMs`, `valid`) and a `std::vector<SensorSlot> slots` parallel to `sensors`; keep the two in step in `addSensor()`, `sortSensors()` and `reserveSensorSlots()` (reserve `MAX_MEASUREMENTS_PER_SENSOR` per slot) (D3)
- [x] 2.3 Add `uint32_t lastDefaultCycleMs` plus a "never run" flag (or sentinel) for the shared default phase (D2)
- [x] 2.4 Split `readSensors()` into a no-arg wrapper calling `readSensors(millis())` and `void readSensors(uint32_t nowMs)` (D8); declare both in `SensorController.h`
- [x] 2.5 In `readSensors(nowMs)`, compute `defaultDue` once, and for each `Online` sensor decide `due` from `requiredIntervalMs()` (D2); set `lastDefaultCycleMs = nowMs` when `defaultDue` was true and at least one default-interval sensor exists
- [x] 2.6 Build `prior` from the union of valid slots before the read loop and refresh it after each successful read so same-tick dependents see fresh values (D4)
- [x] 2.7 On a successful read, replace the sensor's slot contents (measurements plus `Time` entry), set `lastValidMs = nowMs`, `valid = true`; on any attempt set `lastReadMs = nowMs`; always call `recordReadResult()` as today
- [x] 2.8 After the read loop, invalidate slots whose sensor status is not `Online` or whose age exceeds `3 × effectiveInterval` (D5)
- [x] 2.9 Rebuild `allMeasurements` as the concatenation of valid slots in sensor order and swap into `currentMeasurements` under `dataMutex`; set `dataValid = anySlotValid`; set `lastReadingTimestamp` / `lastReadingTime = nowMs` only when at least one read succeeded this call (D5)
- [x] 2.10 Replace `anyI2CSensor` with `anyI2CAttempted` in the recovery logic so the streak is untouched on ticks with no attempted I2C sensor (D6)
- [x] 2.11 Update the `SensorController.h` comment on `lastPidComputeMs` that describes the reading interval as settable, and add a class-level comment describing the slot cache and phase scheduling
- [x] 2.12 Confirm `getMeasurementsCapacity()` and the `reserveSensorSlots()` contract still hold with the union vector (`memory-management` spec "Vector capacities are reserved at boot")

## 3. Native tests (`test/test_sensor_scheduling`)

- [x] 3.1 Add a mock `Sensor::Sensor` with configurable `requiredIntervalMs()`, `providesMeasurements()`, `requiresMeasurements()`, a read counter, a scripted `valid` result, and capture of the last `prior` vector
- [x] 3.2 Test: first `readSensors(t0)` reads both a default sensor and a 1000 ms sensor
- [x] 3.3 Test: default sensor is not read at `t0 + 1000`; 1000 ms sensor is read at `t0`, `t0 + 1000`, `t0 + 2000`
- [x] 3.4 Test: two default sensors are both read at `t0 + 15000` and neither in between (shared phase)
- [x] 3.5 Test: late tick at `t0 + 16000` reads default sensors and `t0 + 30000` does not
- [x] 3.6 Test: measurements persist in `getMeasurements()` across a skipped tick, and `isDataValid()` stays true
- [x] 3.7 Test: failed read while `Online` keeps last-good values
- [x] 3.8 Test: slot cleared when the mock transitions to `ReadFailing` (drive 10 failures)
- [x] 3.9 Test: slot expires at `t0 + 45001` with no valid reading; `isDataValid()` false when all slots expired and `getValidMeasurements()` empty
- [x] 3.10 Test: dependent 1000 ms mock receives the default sensor's `Temperature`/`RelativeHumidity` in `prior` at `t0 + 1000`; and sees the fresh value when both are due on the same tick
- [x] 3.11 Test: `getTemperature()` returns the first sorted sensor's value when two slots provide `Temperature`
- [x] 3.12 Test: `requiredIntervalMs()` returns `0` on the default base and `1000` on `SGP40` (native-buildable header check), and `MEASUREMENT_INTERVAL_MS == 15000`
- [x] 3.13 Run `pio test -e native` and confirm all suites pass, including existing `test_sensor_controller` snapshot and delay tests

## 4. Firmware verification

- [x] 4.1 Build `pio run -e adafruit_qtpy_esp32s2` with no new warnings
- [ ] 4.2 Flash a device with SHT4x + SGP40 (or BME680 + SGP40) and confirm via serial log that default sensors read every 15 s while SGP40 reads every second; VOC index continues to update
- [ ] 4.3 Confirm `/api/sensors` and the MQTT payload keep all measurement types present on every publish, and that `/api/status` `sensor_valid` stays true between default reads
- [ ] 4.4 Disconnect an I2C sensor and confirm bus recovery is not triggered by quiet ticks, and is triggered after three attempted-and-failed default cycles
- [ ] 4.5 Check the "SensorMonitor stack HWM" log line and update the stack-size comment in `src/task/SensorMonitor.cpp` if the measured peak changed
