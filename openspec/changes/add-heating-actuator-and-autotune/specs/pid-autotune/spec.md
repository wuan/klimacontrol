# pid-autotune Specification Delta

## ADDED Requirements

### Requirement: Relay autotune experiment

The firmware SHALL provide an Åström–Hägglund relay autotune that identifies the plant by driving the heating output bang-bang around the setpoint and measuring the induced limit cycle. The output SHALL switch fully on when the temperature falls below `setpoint − h` and fully off when it rises above `setpoint + h`, where `h` is a configurable hysteresis band several times the sensor resolution. The relay method SHALL be used rather than an open-loop step test, because it keeps the room near setpoint throughout and is materially more robust to the load disturbances a real building produces.

#### Scenario: Relay switching

- **WHEN** an autotune is oscillating and the measured temperature falls below `setpoint − h`
- **THEN** the heating output SHALL be commanded fully on

#### Scenario: Relay switching off

- **WHEN** an autotune is oscillating and the measured temperature rises above `setpoint + h`
- **THEN** the heating output SHALL be commanded fully off

#### Scenario: Hysteresis suppresses noise switching

- **WHEN** sensor noise dithers the reading by less than the hysteresis band
- **THEN** no relay transition SHALL occur

### Requirement: Ultimate gain and period calculation

The autotuner SHALL compute the ultimate gain from the relay amplitude and the observed oscillation amplitude, applying the hysteresis correction `Ku = 4d / (π · √(a² − h²))`, where `d` is the relay half-amplitude, `a` is the half peak-to-peak amplitude of the temperature oscillation, and `h` is the hysteresis band. The uncorrected form `4d / (π · a)` SHALL NOT be used, because it over-estimates `Ku` and yields tuning that is too aggressive. The ultimate period `Tu` SHALL be the mean interval between like peaks over the converged cycles.

#### Scenario: Hysteresis correction applied

- **WHEN** `Ku` is computed from a completed run
- **THEN** the hysteresis band SHALL be included in the denominator

#### Scenario: Amplitude smaller than hysteresis

- **WHEN** the observed amplitude `a` is not greater than the hysteresis band `h`
- **THEN** the run SHALL be aborted rather than producing an imaginary or infinite `Ku`

### Requirement: Tyreus–Luyben PI derivation

Gains SHALL be derived using Tyreus–Luyben rules for a PI controller: `Kp = Ku / 3.2`, `Ti = 2.2 · Tu`, `Ki = Kp / Ti`, and `Kd = 0`. Ziegler–Nichols rules SHALL NOT be used, because they target quarter-amplitude damping — roughly 50 % overshoot — which is the wrong objective on a plant whose thermal mass cannot be discharged quickly. The derivative gain SHALL be set to zero because on a lag-dominant underfloor plant it amplifies sensor noise without improving the response.

#### Scenario: Derived gains

- **WHEN** a run converges with `Ku` and `Tu`
- **THEN** the proposed gains SHALL be `Kp = Ku / 3.2`, `Ki = Kp / (2.2 · Tu)` and `Kd = 0`

#### Scenario: Derived gains are range-validated

- **WHEN** a derivation produces a gain outside the accepted configuration range
- **THEN** the run SHALL be reported as failed and the stored gains SHALL remain unchanged

### Requirement: Autotune state machine

The autotuner SHALL expose the states `Idle`, `Settling`, `Oscillating`, `Done` and `Aborted`. A run SHALL begin in `Settling` and SHALL NOT start measuring until the rate of change of temperature falls below a threshold, because a run started on a moving temperature identifies nothing. A run SHALL be considered converged when both the period and the amplitude of three consecutive cycles agree within 15 % and 20 % respectively.

#### Scenario: Settling before measurement

- **WHEN** an autotune is started while the temperature is still changing rapidly
- **THEN** the run SHALL remain in `Settling` until the rate of change falls below the threshold or the settling timeout expires

#### Scenario: Convergence

- **WHEN** three consecutive cycles agree within 15 % on period and 20 % on amplitude
- **THEN** the run SHALL transition to `Done` and report the derived gains

#### Scenario: Only one autotune at a time

- **WHEN** a start is requested while a run is in `Settling` or `Oscillating`
- **THEN** the request SHALL be rejected and the existing run SHALL continue undisturbed

#### Scenario: Normal control is suspended during a run

- **WHEN** a run is in `Oscillating`
- **THEN** the PID SHALL NOT drive the output, and on completion or abort the PID SHALL resume via the existing bumpless-restart path rather than with stale accumulators

### Requirement: Autotune safety envelope

A run SHALL be bounded by an absolute temperature ceiling and floor defaulting to `setpoint ± 3 K` and independent of the normal over-temperature shutoff, by a maximum run duration defaulting to 24 hours, and by immediate abort on loss of valid sensor data. Breaching any bound SHALL abort the run, switch the output off, record a machine-readable reason, and leave the previously stored gains unchanged.

#### Scenario: Temperature ceiling breached

- **WHEN** the measured temperature exceeds the run's ceiling
- **THEN** the run SHALL abort, the output SHALL be switched off, and the stored gains SHALL remain unchanged

#### Scenario: Maximum duration exceeded

- **WHEN** a run has been active for longer than the maximum duration without converging
- **THEN** the run SHALL abort with a reason indicating non-convergence

#### Scenario: Sensor lost mid-run

- **WHEN** sensor data becomes invalid during a run
- **THEN** the run SHALL abort immediately, because a relay experiment without feedback is an uncontrolled open valve

#### Scenario: User abort

- **WHEN** the user aborts a run
- **THEN** the output SHALL be switched off within one control tick and the previously stored gains SHALL remain unchanged

#### Scenario: Minimum dwell still applies

- **WHEN** the relay would switch more often than the actuator minimum dwell permits
- **THEN** the dwell constraint SHALL be honoured, so the experiment cannot chatter the valve

### Requirement: Interrupted runs abort rather than resume

A run interrupted by a reboot SHALL NOT be resumed. The firmware SHALL persist only enough state to recognise that a run was active when the device last stopped, and SHALL report that run as aborted with a reason. Resuming SHALL NOT be offered, because an outage leaves an unobserved gap in the peak record and any period derived across it is not trustworthy.

#### Scenario: Reboot during a run

- **WHEN** the device reboots while a run is in `Settling` or `Oscillating`
- **THEN** on restart the run SHALL be reported as `Aborted` with an interrupted-by-restart reason
- **AND** the stored gains SHALL remain unchanged
- **AND** the output SHALL be off

### Requirement: Derived gains are proposed, not silently applied

A completed run SHALL by default present its derived gains for review alongside the current values, and SHALL apply them only on explicit user acceptance. Automatic application SHALL be available only as an opt-in selected when the run is started.

#### Scenario: Default is propose

- **WHEN** a run started without the auto-apply option completes
- **THEN** the derived gains SHALL be reported and the stored gains SHALL remain unchanged until the user accepts

#### Scenario: Opt-in auto-apply

- **WHEN** a run started with the auto-apply option completes and its derived gains pass range validation
- **THEN** the gains SHALL be persisted and the controller SHALL adopt them

#### Scenario: Accepting proposed gains

- **WHEN** the user accepts the proposed gains
- **THEN** they SHALL be persisted to NVS and the controller SHALL adopt them via the gain-change discontinuity path