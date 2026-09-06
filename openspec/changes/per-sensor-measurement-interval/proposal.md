## Why

The SensorMonitor task reads every online sensor once per second, but nothing downstream needs data that often: the PID computes every 60 s by default, MQTT publishes every 15 s, and the display and web API read a snapshot on demand. The only sensor that genuinely needs 1 Hz is the SGP40, whose Sensirion VOC index algorithm assumes 1 s sampling. Meanwhile 1 Hz reads actively cost us on other sensors: the BME680's `performReading()` fires a 150 ms gas heater on every call (self-heating that biases its temperature, plus 150 ms of blocking bus time per tick), and the SCD4x, PM25, SHT4x and light sensors produce identical or slowly varying values that are read fourteen times more often than they are ever published.

## What Changes

- Add `virtual uint32_t requiredIntervalMs() const` to `Sensor::Sensor`, returning `0` by default. `0` means "read on the system measurement interval"; a non-zero value means "this sensor must be read every N ms regardless".
- Introduce a compile-time constant system measurement interval of 15 000 ms. It is deliberately not a configuration knob.
- `SGP40` overrides `requiredIntervalMs()` to return `1000`.
- `SensorController::readSensors()` reads only sensors that are due. All default-interval sensors become due on the same tick, so temperature, humidity, pressure and dew point remain time-coherent with each other.
- `SensorController` gains an internal per-sensor last-good cache. The published measurement snapshot becomes the union of every cache slot, so measurements from sensors that were not due this tick persist rather than vanishing.
- The `prior` vector handed to dependent sensors (SGP40 needs `Temperature` and `RelativeHumidity`) is built from the cache, not from this tick's accumulator, so a 1 Hz sensor keeps receiving compensation inputs between 15 s reads.
- `isDataValid()` comes to mean "at least one cache slot holds a valid reading", instead of "some sensor returned valid data on this tick".
- The I2C bus-recovery heuristic considers only sensors that were actually attempted this cycle. A tick on which no I2C sensor was due is neither a success nor a failure.
- The `SCD4x` driver's private `co2` cache is removed. It existed only to bridge the sensor's 5 s internal period against 1 Hz polling, which the controller-level cache now handles.
- The per-sensor `Time` measurement keeps its meaning of "duration of the last `read()` call" and is carried in the cache alongside that sensor's other measurements.
- The SensorMonitor task tick stays at 1000 ms and `updateControl()` is unchanged.

Not a breaking change. The HTTP API and MQTT payload shapes are unchanged; no per-measurement age is exposed.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `sensor-management`: adds a requirement that each sensor declares its required read interval and the controller schedules reads accordingly; modifies the `SensorController aggregation` requirement so that the snapshot is a union of last-good readings per sensor and `isDataValid()` reflects cache population rather than per-tick success; adds a requirement for the cache-derived `prior` passed to dependent sensors; qualifies the I2C bus-recovery condition to attempted sensors only.

## Impact

- `src/sensor/Sensor.h`: new virtual `requiredIntervalMs()`.
- `src/sensor/SGP40.h`: override returning 1000.
- `src/sensor/SCD4x.h` / `.cpp`: remove the private `co2` cache; `read()` reports "not valid" when no fresh data is ready rather than replaying the previous value.
- `src/SensorController.h` / `.cpp`: per-sensor cache slots, due-time bookkeeping, the scheduling condition in `readSensors()`, union-building for `currentMeasurements`, revised `dataValid` semantics, revised I2C recovery accounting. `reserveSensorSlots()` capacity contract (see `memory-management` spec) must still hold for the union vector.
- `src/Constants.h` (or `SensorController.h`): the measurement interval constant.
- `src/task/SensorMonitor.cpp`: unchanged tick; its stack high-water comment may need re-measuring since the per-tick work profile changes.
- Consumers (`SensorRoutes.cpp`, `StatusRoutes.cpp`, `DisplayManager.cpp`, `Network.cpp` MQTT publish) are unaffected in shape. They will observe that a measurement's value can now be up to 15 s old; the MQTT publish gate on `isDataValid()` continues to work because the cache stays populated between reads.
- Native tests: `test/test_sensor_controller` gains cases for scheduling, cache union, `prior` from cache, and `dataValid` semantics using mock sensors and the host `millis()` stub in `src/support/Timer.cpp`.
- Behavioural: the BME680 heater fires every 15 s instead of every second; I2C bus occupancy per tick drops to the SGP40 read on 14 of every 15 ticks.
