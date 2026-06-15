## 1. Add `LedState::ERROR` to the status LED

- [x] 1.1 Add `ERROR` to the `LedState` enum in `src/StatusLed.h` with a comment that it displays solid red
- [x] 1.2 Add a `case LedState::ERROR:` branch in `src/StatusLed.cpp::update()` that calls `showColor(0x0F0000)` (solid red); keep the surrounding state-machine structure intact

## 2. Wire `StatusLed` to a top-level object in `main.cpp`

- [x] 2.1 Declare `StatusLed statusLed;` at namespace scope in `src/main.cpp` *before* `SensorController sensorController(config, &statusLed);`
- [x] 2.2 Update `Network`'s constructor to accept a `StatusLed&` (replace the `std::unique_ptr<StatusLed>` member) and remove the `statusLed = std::make_unique<StatusLed>();` line from `Network::task()`; keep `statusLed->begin()` and `statusLed->setState(LedState::STARTUP)` as direct calls on the reference
- [x] 2.3 Update the `Network network(config, sensorController, sensorMonitor);` call in `main.cpp` to pass the new `statusLed` reference

## 3. Update `SensorController` for fail-hard behavior

- [x] 3.1 Update `SensorController`'s constructor signature in `src/SensorController.h` to accept a `StatusLed*` (nullable): `SensorController(Config::ConfigManager &config, StatusLed *statusLed);`
- [x] 3.2 Store the `StatusLed*` as a private member (`statusLed`) in `SensorController`
- [x] 3.3 Rewrite the failure path in `src/SensorController.cpp`: when `dataMutex == nullptr` under `ARDUINO`, log `ESP_LOGE`, set the LED to `ERROR` if `statusLed != nullptr`, `delay(5000)`, then `ESP.restart()`
- [x] 3.4 Add a test-only seam: a `bool didFailMutexInit() const` accessor that returns `dataMutex == nullptr` so native tests can assert the failure path without calling `ESP.restart()`. The accessor is defined unconditionally (not `#ifdef ARDUINO`) so it is testable on the host
- [x] 3.5 Update the `SensorController` constructor call in `main.cpp` to pass `&statusLed`

## 4. Add unit tests

- [x] 4.1 Add a `test/test_status_led_error/` directory with `test_status_led_error.cpp` covering: `setState(LedState::ERROR)` then `getState() == ERROR`; `update()` in `ERROR` state does not crash on native; the `case LedState::ERROR` branch is reached (assert via a `getColorForTest()` accessor or by mocking the pixel)
- [x] 4.2 Add a `test/test_sensor_controller_mutex_init/` directory with `test_sensor_controller_mutex_init.cpp` that constructs a `SensorController` with `StatusLed* = nullptr` and asserts `didFailMutexInit()` returns the expected value (the native build cannot exhaust the FreeRTOS heap, so the test exercises the public accessor rather than forcing the failure)
- [x] 4.3 Wire both new suites into `platformio.ini` under `[env:native] build_src_filter`

## 5. Verify the build and tests

- [x] 5.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the firmware builds with the new constructor signature
- [x] 5.2 Run `pio test -e native` and confirm all existing native tests still pass
- [x] 5.3 Run `pio test -e native -f test_status_led_error` and confirm the new suite passes
- [x] 5.4 Run `pio test -e native -f test_sensor_controller_mutex_init` and confirm the new suite passes
- [x] 5.5 Sanity-check that no other call sites of `SensorController`'s constructor or `Network`'s constructor need to be updated (grep `SensorController(` and `Network(` across `src/` and `test/`)
