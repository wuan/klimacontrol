# Tasks: RelayAutotuner core

## Component

- [x] **`src/control/RelayAutotuner.h`** — Arduino-free, injected clock
  - [x] `AutotuneState`, `AutotuneAbort`, `AutotuneLimits`, `AutotuneResult`
  - [x] `start()`, `cancel()`, `update(temperature, dataValid, nowMs)`
  - [x] Accessors: `state()`, `abortReason()`, `result()`,
        `completedCycles()`, `elapsedMs()`
  - [x] `update()` returns a level (`0.0`/`1.0`), matching
        `PidController::update()` so a caller can treat both uniformly

- [x] **`src/control/RelayAutotuner.cpp`**
  - [x] Settling gate on |dT/dt|, with its own timeout
  - [x] Relay switching with hysteresis around the setpoint
  - [x] Per-cycle peak tracking; cycle bounded by successive ON transitions
  - [x] Convergence: N consecutive cycles within 15 % period, 20 % amplitude
  - [x] `Ku = 4d / (π√(a² − h²))`; abort when `a <= h`
  - [x] Tyreus–Luyben: `Kp = Ku/3.2`, `Ki = Kp/(2.2·Tu)`, `Kd = 0`
  - [x] Reject non-finite or non-positive derived gains
  - [x] Safety envelope: ceiling, floor, run timeout, sensor loss, cancel
  - [x] Aborts are terminal; output forced to zero in every non-oscillating state
  - [x] Unsigned arithmetic throughout, for rollover safety

- [x] Add `+<control/RelayAutotuner.cpp>` to the `native` `build_src_filter`

## Tests — `test/test_relay_autotuner/`

- [x] **FOPDT plant helper** (test fixture, not production code)
  - [x] `dT/dt = (K·u(t−L) − (T − T_ambient)) / τ`, delay queue for `L`
  - [x] Parameterised so several plausible underfloor plants can be driven

- [x] **Convergence**
  - [x] A realistic plant converges and reports plausible `Ku`/`Tu`
  - [x] Identified `Tu` is within a sensible factor of the plant's dead time
  - [x] Converges only after the required number of consistent cycles

- [x] **Derivation arithmetic**
  - [x] Derived gains match hand-computed Tyreus–Luyben for a known `Ku`/`Tu`
  - [x] `Kd` is always zero
  - [x] Hysteresis correction is applied — compare against the uncorrected form
        and assert they differ in the expected direction

- [x] **Settling**
  - [x] A run started on a ramping temperature stays in `Settling`
  - [x] Proceeds once the ramp flattens
  - [x] Settling timeout aborts

- [x] **Abort paths** — one test each, asserting state, reason and zero output
  - [x] Ceiling breached
  - [x] Floor breached
  - [x] Run timeout without convergence
  - [x] Sensor loss mid-run
  - [x] Caller cancel
  - [x] `a <= h` (a plant that barely responds)
  - [x] Aborts are terminal — later updates do not resume

- [x] **Edge cases**
  - [x] Timestamps crossing the `millis()` rollover mid-run
  - [x] Output is zero in `Idle`, `Done` and `Aborted`
  - [x] Two instances do not share state

- [x] `pio test -e native` green (360 cases), existing suites unaffected
- [x] Firmware still builds; nothing links the component, so device behaviour
      is unchanged as scoped

## Findings from implementation

- **`AmplitudeTooSmall` was unreachable as first written.** Guarding only on
  `a² − h² > 0` can never fire: switching uses a strict comparison, so the
  recorded peaks always lie outside the hysteresis band and `a > h` holds for
  any sequence that oscillates at all. The real hazard is `a` merely
  *approaching* `h` — the radicand tends to zero, `Ku` runs away, and the
  derived gains come out enormous yet finite, so they sail past the
  `isfinite`/positivity check. Replaced with a clearance requirement,
  `minAmplitudeRatio` (default 1.2), which is both reachable and protective.
  Found only because the test refused to pass.

## Explicitly out of scope

Recorded so a later reader does not think they were forgotten:

- No wiring into `SensorController` or `SensorMonitor` — nothing consumes the
  commanded level, so a run cannot be started on a device
- No persistence of derived gains; gains stay `constexpr`
- No `/api/autotune/*` endpoints and no UI
- No reboot marker
- No time-proportional output — that belongs with the actuator

At the end of this change the device behaves exactly as it does today.
