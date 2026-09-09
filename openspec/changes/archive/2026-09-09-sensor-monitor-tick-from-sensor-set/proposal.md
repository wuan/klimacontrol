## Why

The Sensor Monitor task wakes every 1000 ms, but on a device without an SGP40 every sensor is read on the shared 15 s phase and the PID computes every `control_interval_s` (60 s by default). Fourteen of every fifteen ticks therefore take the I2C bus lock, allocate the `prior` measurement vector, take the data mutex and return having read nothing. The 1 s cadence is only needed when a sensor declaring a sub-15 s `requiredIntervalMs()` is fitted, and the set of fitted sensors is fixed once `setup()` has scanned the bus.

## What Changes

- `SensorController` gains `minReadIntervalMs()`: the shortest effective read interval over every configured sensor, regardless of status.
- The Sensor Monitor task computes its tick once at start from that value, clamped to `[MIN_TICK_MS, MEASUREMENT_INTERVAL_MS]`, and logs it. With only default-interval sensors the task ticks every 15 s; with an SGP40 fitted it keeps ticking every second; with no sensors it ticks every 15 s so init retries and the control loop's skipped-tick bookkeeping still run.
- Every sleep carries a small `WAKE_MARGIN_MS` so an RTOS wake that lands one tick early cannot leave the default phase 1 ms short of due and skip a whole 15 s cycle.
- The fixed `readingInterval` / `setReadingInterval()` / `getReadingInterval()` on `SensorMonitor` are removed. Nothing outside the class uses them.
- The "The SensorMonitor task tick SHALL remain 1000 ms" sentence in the sensor-management spec is replaced.
- Housekeeping in the same change: both tasks' "stack HWM" diagnostics move from every 5 minutes to every 15 minutes. The spec only requires the line to be periodic, so this needs no spec delta.

## Capabilities

### New Capabilities

_None._

### Modified Capabilities

- `sensor-management`: the "Sensors are read only when due" requirement loses the fixed 1000 ms tick clause; a new requirement defines `minReadIntervalMs()` and the Sensor Monitor's startup-chosen tick.

## Impact

- `src/SensorController.{h,cpp}`: new const query, no change to `readSensors()` semantics.
- `src/task/SensorMonitor.{h,cpp}`: tick selection at task start, wake margin, removal of the unused interval accessors.
- `src/Network.cpp`, `src/task/SensorMonitor.cpp`: diagnostics interval constant.
- `test/test_sensor_scheduling`: native tests for `minReadIntervalMs()`; `test/test_sensor_controller` drops the tests that mirrored the old delay arithmetic.
- Control loop: `TemperatureController::update()` is still called on every tick, so the over-temperature shutoff and the autotuner see every fresh reading. The process value only changes when a sensor is read, so ticks between reads carried no information. With a 15 s tick and the default 60 s interval the PID computes on the first tick at or after the interval, i.e. every 60 s as before.
- `GET /api/about` cycle-delay statistics keep their meaning (milliseconds slept per cycle) but scale with the chosen tick.
