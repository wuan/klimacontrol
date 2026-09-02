# temperature-control Specification Delta

## MODIFIED Requirements

### Requirement: PID parameter configurability

The gains `Kp`, `Ki` and `Kd` SHALL be stored in `DeviceConfig`, persisted to NVS, and validated on load. Defaults SHALL produce a stable response on the target hardware and SHALL be consistent with the tuning method the firmware ships: because the autotuner derives a PI controller with `Kd = 0` by Tyreus–Luyben, the default `Kd` SHALL be zero and the default `Ki` SHALL be of an order suited to a plant whose time constant is hours rather than seconds.

A gain change SHALL be treated as a discontinuity: the controller SHALL be suspended so that the next tick restarts bumplessly, because an integral accumulated under the old gains does not mean the same thing under the new ones. Reusing the bumpless-restart path SHALL be preferred to introducing a second way to reset controller state.

Gains SHALL be applied to the running controller only by the task that owns it. A gain change originating on another task SHALL be handed over as a request consumed on the control task, rather than written directly into controller state, so that the controller remains single-writer.

`Kp` SHALL NOT be permitted to be zero, because a zero proportional gain disables control while control still reports itself as enabled. `Ki` and `Kd` MAY be zero.

#### Scenario: Tuning gains

- **WHEN** new gain values are loaded
- **THEN** subsequent `updateControl()` calls SHALL use the new values

#### Scenario: Gains survive a restart

- **WHEN** gains are stored and the device restarts
- **THEN** the stored gains SHALL be in force rather than the compiled-in defaults

#### Scenario: A gain change suspends the controller

- **WHEN** any gain is changed while the controller is running
- **THEN** the controller SHALL be suspended and the next computing tick SHALL restart bumplessly with a zero integral

#### Scenario: A cross-task gain change is deferred

- **WHEN** a gain change is requested from the web server task
- **THEN** it SHALL be applied by the control task on a subsequent tick, and the requesting task SHALL NOT write controller state directly

#### Scenario: Invalid gains fall back rather than refusing to boot

- **WHEN** a persisted gain is absent, non-finite or outside its documented range
- **THEN** that field SHALL fall back to its default and the device SHALL boot normally

#### Scenario: Zero proportional gain is refused

- **WHEN** a `Kp` of zero is submitted
- **THEN** it SHALL be rejected and the stored gains SHALL be unchanged

### Requirement: Control loop scheduling

The control loop SHALL run inside the Sensor Monitor task on each sensor read cycle (1-second cadence by default). On each iteration the controller SHALL call `updateControl()`. The call SHALL be skipped when control is disabled or when no valid sensor data is available; in those cases the controller's effective output SHALL be `0.0`.

`updateControl()` SHALL be invoked on every sensor read cycle regardless of the control interval, so that a cycle on which the loop declines to compute — because control is disabled, no valid reading exists, the over-temperature shutoff is engaged, or an autotune run owns the output — is still marked as a skipped tick. Decimating the invocation rather than the computation SHALL NOT be done, because the next computing tick would then measure an elapsed time spanning the whole gap and saturate the integral term on that tick.

A cycle on which the PID merely does not compute *because the control interval has not yet elapsed* SHALL NOT be marked as skipped. Marking it would make every computation a bumpless restart, so the integral accumulator would be discarded before it could ever carry from one computation to the next and `Ki` would have no effect at any configured value. Such a cycle SHALL leave the accumulated controller state untouched, and the elapsed time the next computation measures SHALL be the real interval since the last computation.

The PID computation SHALL be decimated to a configurable control interval, defaulting to 60 seconds, because a plant whose time constant is measured in hours does not benefit from a 1-second loop and the derivative term at that cadence responds mostly to sensor noise. The interval SHALL be a lower bound on the spacing between computations rather than a schedule: a late tick SHALL compute late, and the elapsed time SHALL be measured rather than assumed. Interval arithmetic SHALL remain correct across the `millis()` rollover.

The following SHALL NOT be decimated, and SHALL be evaluated on every sensor read cycle:

- The over-temperature shutoff, because a safety limit observed up to a full control interval late is a weaker guarantee than the safety-limits requirement describes.
- The autotuner's update while a run is active, because it measures the amplitude of an induced oscillation and coarser sampling biases that measurement.

#### Scenario: No valid data

- **WHEN** all sensors are in `InitFailed` or `ReadFailing` state
- **THEN** `updateControl()` SHALL not be invoked and the controller output SHALL be reported as `0.0`

#### Scenario: PID computes on the control interval

- **WHEN** the control interval is 60 seconds and sensors read every second
- **THEN** the PID SHALL compute approximately once per 60 sensor reads

#### Scenario: Skipped ticks are still marked

- **WHEN** a sensor tick occurs on which the loop declines to compute because control is disabled, no valid reading exists, the shutoff is engaged, or a run is active
- **THEN** `updateControl()` SHALL still be invoked, the tick SHALL be marked as skipped, and the controller SHALL NOT subsequently measure an elapsed time spanning the skipped ticks

#### Scenario: A merely decimated tick is not a skipped tick

- **WHEN** a sensor tick occurs on which the PID does not compute only because the control interval has not yet elapsed
- **THEN** the controller SHALL NOT be suspended, and the integral accumulated so far SHALL survive to the next computation

#### Scenario: Integral action survives decimation

- **WHEN** the controller runs for several control intervals against a sustained error with a non-zero `Ki`
- **THEN** the integral term SHALL have accumulated, rather than having been reset on each computation

#### Scenario: Safety shutoff is not delayed by the control interval

- **WHEN** the temperature crosses the over-temperature limit with a 60-second control interval configured
- **THEN** the shutoff SHALL engage within one sensor read cycle rather than waiting for the next PID computation

#### Scenario: Autotune sampling is not decimated

- **WHEN** an autotune run is active with a 60-second control interval configured
- **THEN** the autotuner SHALL be updated on every sensor read cycle

#### Scenario: Interval survives the rollover

- **WHEN** the control interval elapses across the `millis()` rollover
- **THEN** the PID SHALL compute on schedule rather than stalling for the length of the counter
