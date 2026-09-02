# pid-autotune Specification Delta

## ADDED Requirements

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

### Requirement: Derived gains are applied in memory only

Accepting a derived result SHALL apply the gains to the running controller and SHALL suspend it, treating the change as a discontinuity. The gains SHALL NOT be persisted, because no configuration field exists for them; a restart SHALL return the compiled-in defaults. Any interface offering acceptance SHALL make that plain rather than implying the change is durable.

#### Scenario: Accepting applies and suspends

- **WHEN** a derived result is accepted
- **THEN** the controller SHALL adopt the gains and restart bumplessly on its next tick

#### Scenario: Accepted gains do not survive a restart

- **WHEN** the device restarts after a result was accepted
- **THEN** the compiled-in default gains SHALL be in force

#### Scenario: Accepting without a result

- **WHEN** acceptance is requested and no converged result exists
- **THEN** the request SHALL be refused and the gains SHALL be unchanged
