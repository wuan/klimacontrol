# pid-autotune Specification

## Purpose
TBD - created by archiving change add-relay-autotuner-core. Update Purpose after archive.
## Requirements
### Requirement: Relay autotune identification component

The firmware SHALL provide a `Control::RelayAutotuner` component that identifies the plant by commanding the heating output bang-bang around the setpoint and measuring the induced limit cycle. It SHALL be free of Arduino and FreeRTOS dependencies and SHALL take the current time as a parameter rather than reading a clock, so that the whole state machine builds and runs in the `native` environment.

The component SHALL perform no I/O, SHALL NOT persist anything, and SHALL NOT apply its result. It reports a commanded output level and a state; acting on either is the caller's responsibility.

#### Scenario: Relay switches on hysteresis

- **WHEN** a run is oscillating and the measured temperature falls below `setpoint − h`
- **THEN** the commanded output SHALL be full on

#### Scenario: Relay switches off

- **WHEN** a run is oscillating and the measured temperature rises above `setpoint + h`
- **THEN** the commanded output SHALL be full off

#### Scenario: Hysteresis suppresses noise switching

- **WHEN** the reading dithers by less than the hysteresis band
- **THEN** no transition SHALL occur

#### Scenario: No output outside a run

- **WHEN** the state is `Idle`, `Done` or `Aborted`
- **THEN** the commanded output SHALL be zero

### Requirement: Settling before measurement

A run SHALL begin in a settling state and SHALL NOT record peaks until the rate of change of temperature falls below a configured threshold, because a run started on a moving temperature identifies the disturbance rather than the plant. Settling SHALL have its own timeout, distinct from the overall run timeout.

#### Scenario: Waits for a steady temperature

- **WHEN** a run is started while the temperature is changing faster than the threshold
- **THEN** the state SHALL remain `Settling`

#### Scenario: Proceeds once steady

- **WHEN** the rate of change falls below the threshold
- **THEN** the state SHALL become `Oscillating`

#### Scenario: Settling has its own timeout

- **WHEN** the temperature never settles within the settling timeout
- **THEN** the run SHALL abort with a settling-timeout reason

### Requirement: Ultimate gain and period calculation

The component SHALL compute the ultimate gain with the hysteresis correction `Ku = 4d / (π · √(a² − h²))`, where `d` is the relay half-amplitude, `a` is the half peak-to-peak amplitude of the observed oscillation and `h` is the hysteresis band. The uncorrected form `4d / (π · a)` SHALL NOT be used, because it over-estimates `Ku` and yields tuning that is too aggressive for a plant that cannot shed heat quickly. The ultimate period `Tu` SHALL be the mean interval between like transitions over the converged cycles.

#### Scenario: Hysteresis correction applied

- **WHEN** `Ku` is computed for a converged run
- **THEN** the hysteresis band SHALL be included in the denominator

#### Scenario: Amplitude within the hysteresis band

- **WHEN** the observed amplitude `a` is not greater than the hysteresis band `h`
- **THEN** the run SHALL abort rather than evaluate a square root of a negative number

### Requirement: Convergence criteria

A run SHALL be considered converged when a configured number of consecutive cycles, defaulting to three, agree within 15 % on period and 20 % on amplitude. Fewer than three SHALL NOT be accepted as converged, because two cycles can agree by coincidence while the plant is still settling.

#### Scenario: Converges on consistent cycles

- **WHEN** three consecutive cycles agree within tolerance
- **THEN** the state SHALL become `Done` and the result SHALL be populated

#### Scenario: Inconsistent cycles do not converge

- **WHEN** successive cycles disagree beyond tolerance
- **THEN** the run SHALL remain `Oscillating` until it converges or times out

### Requirement: Tyreus–Luyben PI derivation

Derived gains SHALL use Tyreus–Luyben rules for a PI controller: `Kp = Ku / 3.2`, `Ti = 2.2 · Tu`, `Ki = Kp / Ti`, and `Kd = 0`. Ziegler–Nichols SHALL NOT be used, because it targets quarter-amplitude damping — roughly 50 % overshoot — which is the wrong objective on a plant whose thermal mass cannot be discharged quickly. `Kd` SHALL be zero because on a lag-dominant plant the derivative term amplifies sensor noise without improving the response.

`Ki` SHALL be expressed in the same parameterisation `Control::PidController` consumes, so a derived result can be used without conversion.

#### Scenario: Derived gains

- **WHEN** a run converges with a known `Ku` and `Tu`
- **THEN** the result SHALL be `Kp = Ku / 3.2`, `Ki = Kp / (2.2 · Tu)` and `Kd = 0`

#### Scenario: Implausible derived gains fail the run

- **WHEN** a derivation produces a non-finite or non-positive gain
- **THEN** the run SHALL abort rather than report a result

### Requirement: Autotune safety envelope

A run SHALL be bounded by an absolute temperature ceiling and floor relative to the setpoint, by a maximum run duration, and by immediate abort on loss of valid sensor data. Breaching any bound SHALL abort the run, force the commanded output to zero, and record a machine-readable reason. A run SHALL also be cancellable by the caller at any time.

#### Scenario: Temperature ceiling breached

- **WHEN** the measured temperature exceeds the run's ceiling
- **THEN** the run SHALL abort and the commanded output SHALL be zero

#### Scenario: Temperature floor breached

- **WHEN** the measured temperature falls below the run's floor
- **THEN** the run SHALL abort and the commanded output SHALL be zero

#### Scenario: Maximum duration exceeded

- **WHEN** a run has been active longer than the maximum duration without converging
- **THEN** the run SHALL abort with a non-convergence reason

#### Scenario: Sensor lost mid-run

- **WHEN** sensor data becomes invalid during a run
- **THEN** the run SHALL abort immediately, because a relay experiment without feedback is an uncontrolled output

#### Scenario: Caller cancels

- **WHEN** the caller cancels a run
- **THEN** the state SHALL become `Aborted` with a user-requested reason and the commanded output SHALL be zero

#### Scenario: Aborts are terminal

- **WHEN** a run has aborted
- **THEN** further updates SHALL keep the commanded output at zero and SHALL NOT resume measurement

### Requirement: Elapsed time is rollover-safe

Every interval the component measures SHALL be computed with unsigned arithmetic on the supplied timestamps, so that a run spanning the `millis()` rollover at approximately 49.7 days measures true intervals rather than an apparent jump of about 4.29e6 seconds.

#### Scenario: Run spanning the rollover

- **WHEN** a run's timestamps cross the 32-bit millisecond wrap
- **THEN** measured cycle periods and elapsed time SHALL remain correct

### Requirement: Autotune owns the control output while running

The autotuner SHALL drive the controller's output whenever a run is active, and the PID SHALL NOT drive it at the same time. The PID SHALL be suspended for the duration of a run, so that when the run ends the PID resumes through the existing bumpless-restart path rather than with accumulators and a timestamp that predate the run.

#### Scenario: Autotune drives the output during a run

- **WHEN** a run is settling or oscillating
- **THEN** the controller's reported output SHALL be the autotuner's commanded level

#### Scenario: PID resumes bumplessly afterwards

- **WHEN** a run ends, whether converged or aborted
- **THEN** the PID SHALL treat its next tick as a restart, with a zeroed integral and a reseated timestamp

#### Scenario: Control disabled mid-run

- **WHEN** temperature control is disabled while a run is active
- **THEN** the run SHALL be cancelled and the output SHALL be zero

### Requirement: Runs are started and cancelled by request

Starting and cancelling a run SHALL be requested by setting a flag that the control-loop task consumes, not by calling the autotuner directly from another task. Only the task that owns the control loop SHALL mutate autotuner state, for the same reason the PID accumulators are single-writer: a concurrent write can be straddled by an in-progress update and silently lost.

A pending request SHALL be consumed exactly once. When a cancel and a start are both pending, the cancel SHALL be applied first, so the outcome does not depend on arrival order within a tick.

#### Scenario: Request is consumed once

- **WHEN** a start is requested
- **THEN** exactly one run SHALL begin, on the next control tick

#### Scenario: Acknowledged before it takes effect

- **WHEN** a start is requested
- **THEN** the run may not yet be active on the immediately following status read
- **AND** the status endpoint SHALL remain the authority on whether a run is active

### Requirement: Derived gains are persisted when accepted

Accepting a derived result SHALL persist the gains and apply them to the running controller, suspending it so that the change is treated as a discontinuity. The gains SHALL survive a restart, because a run costs hours of the plant's time and a result whose lifetime is shorter than the experiment that produced it is not worth the experiment.

Persistence SHALL precede application, so that the gains a user is shown as being in force are the gains that will survive a restart.

Acceptance SHALL remain an explicit request rather than being applied automatically when a run converges. A run that silently rewrote the gains of a heating system while nobody was watching is a poor default, and derived values are worth a human glance given what they cost to obtain.

Because the gains are applied by the task that owns the controller, acceptance SHALL be reported as a request that was validated and accepted rather than as a change already applied. The gains actually in force SHALL be observable separately, so that a caller can confirm the outcome.

#### Scenario: Accepting applies and suspends

- **WHEN** a derived result is accepted
- **THEN** the controller SHALL adopt the gains and restart bumplessly on its next tick

#### Scenario: Accepted gains survive a restart

- **WHEN** the device restarts after a result was accepted
- **THEN** the accepted gains SHALL be in force rather than the compiled-in defaults

#### Scenario: Accepting without a result

- **WHEN** acceptance is requested and no converged result exists
- **THEN** the request SHALL be refused, and neither the stored nor the running gains SHALL change

#### Scenario: Convergence alone does not change the gains

- **WHEN** a run converges and no acceptance is requested
- **THEN** the gains in force SHALL be unchanged, and the derived values SHALL be reported as available for acceptance

#### Scenario: A failed write does not leave divergent state

- **WHEN** persisting an accepted result fails
- **THEN** the running controller SHALL NOT be left using gains that would not survive a restart

#### Scenario: The applied result is observable

- **WHEN** acceptance has been requested and accepted
- **THEN** the gains in force SHALL be readable, so that the caller can confirm the control task applied them
