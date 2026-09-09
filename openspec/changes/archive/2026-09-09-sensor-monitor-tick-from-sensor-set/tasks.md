## 1. Controller query

- [x] 1.1 Add `uint32_t minReadIntervalMs() const` to `SensorController` (`src/SensorController.h/.cpp`): minimum effective interval over all configured sensors regardless of status, `MEASUREMENT_INTERVAL_MS` when none.
- [x] 1.2 Native tests in `test/test_sensor_scheduling`: default sensors only, one-hertz sensor wins, offline sensor still counts, no sensors.

## 2. Sensor Monitor task

- [x] 2.1 In `SensorMonitor::task()` compute `tickMs` once at start from `minReadIntervalMs()` clamped to `[MIN_TICK_MS, MAX_TICK_MS]`, log it, and `static_assert(MAX_TICK_MS == MEASUREMENT_INTERVAL_MS)`.
- [x] 2.2 Sleep `tickMs - elapsed + WAKE_MARGIN_MS` (or one RTOS tick on overrun); document the margin at its definition.
- [x] 2.3 Remove `readingInterval`, `setReadingInterval()`, `getReadingInterval()` from `SensorMonitor.h`.
- [x] 2.4 Drop the `computeDelay` mirror tests in `test/test_sensor_controller` that encoded the old fixed-interval arithmetic.

## 3. Diagnostics interval

- [x] 3.1 Change `DIAGNOSTICS_INTERVAL_MS` from 300000 to 900000 in `src/Network.cpp` and `src/task/SensorMonitor.cpp`, comments included.

## 4. Verification

- [x] 4.1 `pio test -e native` passes.
- [x] 4.2 `pio run -e adafruit_qtpy_esp32s2` builds.
- [x] 4.3 `openspec validate --all --strict` passes from the repo root.
