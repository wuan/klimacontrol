## Why

`SensorController`'s constructor calls `xSemaphoreCreateMutex()` to create the
mutex that guards `currentMeasurements` between the Sensor Monitor and Network
tasks. When the heap is exhausted at boot, the FreeRTOS call returns `nullptr`
and the constructor only logs `ESP_LOGE` before continuing. Every accessor is
guarded with `dataMutex && …`, so a null handle silently degrades to "lock
unavailable" — readers get empty/default snapshots, writers skip the protected
section, and the device looks healthy (web UI shows a connected board, no
errors in the API) while producing no actual sensor data. This is a textbook
silent-degradation bug: a critical init-time failure that the user can only
diagnose by noticing the absence of data hours later.

## What Changes

- Make `SensorController` fail hard when `xSemaphoreCreateMutex()` returns
  `nullptr`: log the error, switch the status LED to a new `ERROR` state
  (solid red), hold for a short grace period so the indicator is visible,
  then call `ESP.restart()`.
- Add a new `LedState::ERROR` value to `src/StatusLed.h` that displays solid
  red and updates the state-to-behavior mapping in `src/StatusLed.cpp` to
  drive the pixel red in that state.
- Wire the `SensorController` constructor to take a `StatusLed*` (or
  equivalent notifier) so it can set the LED on the failure path. The
  pointer is allowed to be `nullptr` (e.g. in unit tests) — in that case
  the controller skips the LED step but still restarts.
- Document the new precondition: `SensorController` MUST be constructed
  *after* the `StatusLed` is created so the LED is available on the
  failure path. (Today the LED is created in `Network`'s constructor
  before the `SensorController`, so wiring stays simple.)
- Add a unit test for the new `LedState::ERROR` mapping (e.g.
  `getState() == ERROR` after `setState(LedState::ERROR)`).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `sensor-management`: add a new **Mutex initialization precondition** requirement — `SensorController` MUST treat a failed `xSemaphoreCreateMutex()` as a fatal init error: log it, drive the status LED to `ERROR`, wait a short grace period, then restart the device. The current "log and continue" behavior is removed.
- `status-led`: extend the **State machine** requirement with a new `ERROR` value, and extend the **State-to-behavior mapping** requirement with `ERROR` → solid red. Add a scenario asserting the new mapping.

## Impact

- **Code**
  - `src/SensorController.h` — constructor takes a `StatusLed*` (nullable).
  - `src/SensorController.cpp` — failure path: log, set LED, `delay()`, `ESP.restart()`.
  - `src/StatusLed.h` — add `LedState::ERROR` to the enum.
  - `src/StatusLed.cpp` — `setState(ERROR)` updates the color to solid red.
  - `src/main.cpp` — pass the `StatusLed*` (already constructed) into
    `SensorController`'s constructor.
- **APIs**: `SensorController`'s constructor gains a `StatusLed*` parameter.
  This is a source-level change; the runtime layout is unaffected. Existing
  call sites in `main.cpp` and any tests are updated.
- **Behavior**: identical in the success path. In the failure path the device
  reboots a few seconds after boot instead of running broken. The LED is
  visible during the grace period so a human can see the cause.
- **Tests**: add a `test_status_led_error` suite covering the new state.
  Add a unit test for the `SensorController` failure path on native
  (mocking the mutex creation by injecting a factory function or by
  pre-allocating the failure mode via a test-only seam).
- **Dependencies**: none. `LEDState::ERROR` reuses the existing
  `Adafruit_NeoPixel` and `millis()` plumbing.
