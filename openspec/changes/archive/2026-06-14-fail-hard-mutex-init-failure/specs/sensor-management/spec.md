## ADDED Requirements

### Requirement: Mutex initialization precondition

`SensorController` SHALL treat a failed `xSemaphoreCreateMutex()` as a fatal init error. On the failure path the controller SHALL:

- log an `ESP_LOGE` line identifying the failure mode (heap exhausted at boot);
- drive the status LED to the `ERROR` state (solid red);
- hold for a short grace period (5 s) so the LED indicator is visible to a human;
- call `ESP.restart()` to give the device a chance to recover from transient boot-time heap pressure.

The previous "log and continue" behavior — where data accessors silently return defaults because `dataMutex` is `nullptr` — is removed.

The `StatusLed*` passed to the constructor MAY be `nullptr` (e.g. in native unit tests); in that case the LED step is skipped and the controller still logs and restarts under `ARDUINO`. The native build (no `ARDUINO` defined) SHALL NOT call `ESP.restart()`; the failure SHALL be observable as a non-null return from the constructor's failure-detection helper for tests to assert on.

#### Scenario: Mutex allocation fails at boot

- **WHEN** the heap is exhausted at boot and `xSemaphoreCreateMutex()` returns `nullptr` from the `SensorController` constructor
- **THEN** an `ESP_LOGE` line is logged, the status LED transitions to the `ERROR` state, the controller holds for 5 s, and `ESP.restart()` is called

#### Scenario: Mutex allocation succeeds at boot

- **WHEN** the heap is healthy and `xSemaphoreCreateMutex()` returns a valid handle from the `SensorController` constructor
- **THEN** no log line is emitted on the failure path, the LED is not driven to `ERROR`, and the controller proceeds with normal initialization
