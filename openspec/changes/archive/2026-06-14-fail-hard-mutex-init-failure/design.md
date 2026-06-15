## Context

`SensorController`'s constructor (in `src/SensorController.cpp:25-40`) calls
`xSemaphoreCreateMutex()` to create the mutex that guards `currentMeasurements`
between the Sensor Monitor and Network tasks. When the FreeRTOS heap is
exhausted at boot, the call returns `nullptr`. The constructor only logs
`ESP_LOGE` and continues.

The mutex is referenced on every read (`getMeasurements`, `getValidMeasurements`,
`getSnapshot`, `getFloatMeasurement`, `getIntMeasurement`, `getTemperature`,
`getRelativeHumidity`, `getDewPoint`, `getVocIndex`, `getLastReadingTimestamp`,
`isDataValid`) and on every write (`readSensors`, snapshot updates). Every
guard is written `dataMutex && (xSemaphoreTake(...) == pdTRUE)`, so a null
handle silently degrades to "lock unavailable" — readers get the default
empty/NaN snapshot, writers skip the protected section. From the outside the
device looks healthy: the web UI shows a connected board, the API responds
200 OK with empty data arrays, no error is reported. Only someone watching
the trend graph for hours would notice that no actual measurement was ever
recorded.

There is a second, structural complication. The status LED is created inside
`Network::task()` at `src/Network.cpp:332` (a FreeRTOS task body), *not* in
the `Network` constructor. By the time `sensorController.begin()` runs in
`main.cpp:111`, the LED may not exist yet (the network task may not have
been scheduled). Even if we want to use the LED to indicate the failure, we
have to ensure the LED is alive at the moment `SensorController` decides
it has failed.

## Goals / Non-Goals

**Goals:**

- Make a failed `xSemaphoreCreateMutex()` a fatal init error: the controller
  must not continue to run with a degraded lock.
- Surface the failure visibly to a human looking at the device (the status
  LED) and to a human reading the logs (`ESP_LOGE`).
- Give the system a chance to recover from transient boot-time heap pressure
  by restarting.
- Keep the public API of `SensorController` (excluding the constructor
  signature) byte-for-byte the same; the rest of the codebase does not need
  to change.
- Add a unit test for the new `LedState::ERROR` state.

**Non-Goals:**

- Refactoring the rest of `SensorController`'s locking strategy (e.g.
  moving to a `portMUX`, replacing the mutex with a `std::shared_mutex`, or
  guarding every accessor with try-locks and falling back to defaults). The
  failure path is the only thing changing.
- Adding a generic "fatal init" framework for the rest of the firmware.
- Adding a new task or scheduler hook.
- Changing the LED color in any state other than `ERROR`.

## Decisions

### Decision 1 — Fail hard with `ESP.restart()`, not a degraded mode

**Choice**: When the mutex creation fails, log the error, set the LED to
`ERROR`, hold for a short grace period (5 s) so a human can see it, then
call `ESP.restart()`.

**Rationale**: Heap exhaustion at boot is almost never a transient state
that the rest of the firmware can work around — if `xSemaphoreCreateMutex()`
fails, the next `xQueueCreate`, `xTaskCreate`, `malloc`, or even a
`String::operator+` is likely to fail too. A "degraded mode" that returns
empty snapshots is exactly the silent-degradation bug we are trying to
fix. Restarting gives the device a chance to recover from a transient
allocation failure (e.g. another boot-time allocation right before the
mutex) and forces a visible side effect (the LED + a fresh boot).

**Alternatives considered**:

- **Degraded mode with red LED** — leaves the bug in place: the API still
  answers with empty data, MQTT still publishes zeros, the user still has
  to notice. The user message explicitly offers this as a fallback, not
  the primary recommendation.
- **Block forever in the constructor with a red LED** — turns a recoverable
  transient into a permanent hang. The user has to power-cycle the device
  to recover. Worse than restart.
- **Throw a `std::exception` from the constructor** — `xSemaphoreCreateMutex`
  is a C API; wrapping it in a C++ exception is fine, but no caller of
  `SensorController` currently catches it, and the global instance in
  `main.cpp:38` cannot be guarded by `try`/`catch` at namespace scope.
  Restart is simpler and gives the same recovery semantics.

### Decision 2 — Add `LedState::ERROR` (solid red)

**Choice**: Add a new value `ERROR` to the `LedState` enum in
`src/StatusLed.h` and map it to solid red in `src/StatusLed.cpp::update()`.

**Rationale**: The existing `LedState` values (`OFF`, `ON`, `STARTUP`,
`TRANSMIT_DATA`) all encode "normal" or "boot" or "active" semantics. None
of them is a "something is wrong, look at me" indicator. The AGENTS.md
already describes a "Red: Error or warning condition" state for the LED,
but the implementation never had one — adding `ERROR` is filling a
documented gap, not a brand-new convention.

**Alternatives considered**:

- **Reuse `STARTUP` for the error** — wrong semantics; a user looking at a
  fast-blinking blue LED would assume the device is still booting, not
  that it has just detected a fatal init error.
- **Reuse `ON` (green→red gradient) with `progress = 1.0`** — solid red,
  but indistinguishable from "MQTT publish overdue" (which is the only
  way `ON` reaches `progress = 1.0`). Wrong semantics again.
- **Pulse the LED rapidly** — nice for visibility, but harder to test
  deterministically (the test would have to sample `update()` at a
  specific moment). Solid red is testable and easy to recognize.

### Decision 3 — Move `StatusLed` to a top-level object in `main.cpp`

**Choice**: Declare `StatusLed statusLed;` at namespace scope in
`src/main.cpp`, before `SensorController`. Pass a non-owning `StatusLed*`
to `SensorController`'s constructor and a `StatusLed&` to `Network`.
Remove the `statusLed = std::make_unique<StatusLed>();` line from
`Network::task()`.

**Rationale**: `SensorController`'s failure path needs the LED to be alive
during its constructor. The LED is currently constructed inside
`Network::task()`, which is a FreeRTOS task body that may not have been
scheduled by the time `sensorController.begin()` runs. Hoisting the LED to
a global object constructed at boot (before `SensorController`) makes the
ordering deterministic and removes the race. The LED is a single global
piece of state with no concurrent owners, so a non-owning pointer/reference
is the right shape.

**Alternatives considered**:

- **Pass the LED to `SensorController` as a setter (`setStatusLed(&led)`)**
  called from `Network::startTask()` before `sensorController.begin()` —
  works, but the constructor-vs-setter ordering is fragile and easy to
  break with a future refactor. A constructor parameter is a hard
  contract.
- **Lazy-init the LED inside `SensorController::begin()` if it's null** —
  duplicates ownership of the LED between `SensorController` and `Network`,
  and breaks the "single owner" rule from the system-architecture spec.
- **Move the failure check from the constructor to `begin()`** — works,
  but `begin()` runs after the LED is constructed in this design, so we
  could put the failure check there. The proposal keeps the check in the
  constructor because that is where the mutex is created; deferring the
  check to `begin()` would mean either creating the mutex in `begin()`
  (also fine) or checking after the fact (ugly). The proposal picks the
  constructor because it is the existing location of the failure and
  matches the user's mental model.

### Decision 4 — Grace period: 5 s, then `ESP.restart()`

**Choice**: After setting the LED, `delay(5000)` then `ESP.restart()`.

**Rationale**: 5 s is long enough for a human within arm's reach of the
device to glance at it and notice the red LED, and short enough that an
automated test or a remote monitor will not be confused for long. On a
healthy boot the path is never taken, so the cost of 5 s of wall time only
hits the failure case.

**Alternatives considered**:

- **No delay, immediate `ESP.restart()`** — the LED may not even reach
  steady red before the MCU resets; a person looking at the device may see
  only a brief flash or nothing at all. Defeats the purpose of the LED
  indicator.
- **30 s delay** — too long; the device is broken and there is no value
  in making the failure visible for half a minute.

### Decision 5 — `StatusLed*` may be `nullptr` (test seam)

**Choice**: `SensorController`'s constructor accepts a `StatusLed*` and
guards the LED call with `if (led) led->setState(LedState::ERROR);`.

**Rationale**: Native unit tests construct `SensorController` without a
real `StatusLed` (and the LED's `Adafruit_NeoPixel` member is `#ifdef
ARDUINO` anyway). Treating `nullptr` as "skip the LED step" lets the
failure-path test exercise the controller's logic on the host without
having to mock the LED.

**Alternatives considered**:

- **Require a non-null `StatusLed&`** — forces every test to instantiate a
  StatusLed. The LED is harmless on native (the `Adafruit_NeoPixel` member
  is `#ifdef ARDUINO`-gated) but the pattern of "non-owning reference as
  required dependency" is heavier than "non-owning pointer as optional
  collaborator".
- **Provide a no-op `NullStatusLed` stub for tests** — premature
  abstraction; the controller only needs to call `setState`, so a single
  null check is simpler.

## Risks / Trade-offs

- **[Risk]** `ESP.restart()` from a global constructor at boot can race
  with the C runtime if the constructor runs before `setup()`. →
  **Mitigation**: `SensorController` is a namespace-scope instance in
  `main.cpp`, constructed after the standard library globals (which is the
  same ordering already in place). `ESP.restart()` from a constructor is
  only reached if the mutex allocation fails, which means the standard
  library is otherwise healthy enough to log and call `delay()`. The 5 s
  delay also gives the C runtime time to settle.
- **[Risk]** A 5 s `delay()` from a constructor is a long busy-wait that
  blocks the main thread, including any pending FreeRTOS task creation
  scheduled for after the constructor returns. → **Mitigation**: The only
  call sites that run after the constructor are `setup()` (where the
  `delay(1000)` is already there) and FreeRTOS task bodies (which are
  preempted by `delay()` only if they yield; if they don't, the device
  was broken anyway). 5 s of blocking at boot is acceptable on a fatal
  error path.
- **[Risk]** Adding `LedState::ERROR` is a binary-incompatible change to
  the enum; any saved value that includes an `LedState` in NVS or over the
  wire would be wrong after the bump. → **Mitigation**: The enum is
  in-memory only; nothing persists it. The compile-time size of the
  enum grows by 1 (4 → 5), which is irrelevant.
- **[Risk]** A boot loop caused by persistent heap exhaustion (e.g.
  firmware regression that always allocates too much at boot) would make
  the device reboot forever with no human intervention. → **Mitigation**:
  This is the *desired* behavior: a permanent boot loop is far more
  diagnosable than a silent broken device. The user can pull the USB
  cable, reflash via the BOOT button, and inspect the serial log to see
  the `ESP_LOGE` line.

## Migration Plan

1. Land the change in one PR. The constructor signature change is
   source-level only; no NVS or API impact.
2. Run `pio run -e adafruit_qtpy_esp32s2` to confirm the firmware still
   builds.
3. Run `pio test -e native` to confirm the existing native suites still
   pass, including the new `test_status_led_error` cases.
4. The LED color and the new `ERROR` state are exercised manually: power
   the device, observe green during normal operation; in the failure case
   the LED will turn red and stay red for 5 s before the device reboots.
5. Rollback is `git revert`; no persistent state is touched.

## Open Questions

- None. The fix is local, the LED mapping is straightforward, and the
  ordering change in `main.cpp` is mechanical.
