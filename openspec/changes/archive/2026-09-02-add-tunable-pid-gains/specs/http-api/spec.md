# http-api Specification Delta

## ADDED Requirements

### Requirement: Tuning write endpoint

The firmware SHALL expose `POST /api/control/tuning`, accepting `kp`, `ki`, `kd` and the control interval, and requiring the `X-Requested-With: KlimaControl` CSRF header.

Validation SHALL be all-or-nothing and SHALL **reject** rather than clamp: if any field is absent, unparseable or outside its documented range, the endpoint SHALL respond with an error naming the offending field and SHALL apply nothing. Clamping a value the caller did not ask for is worse here than refusing it, because the result drives a physical valve and a silently altered gain is indistinguishable from an accepted one.

No separate read endpoint SHALL be added. The tuning values SHALL be read from `GET /api/control`, which already reports the gains.

The handler SHALL be registered with an exact URI matcher. `server.on(const char*)` builds a matcher that also matches any deeper path, and because handlers are tried in registration order the already-registered `GET /api/control` would otherwise shadow paths beneath it — the failure mode this project has already debugged once for `/api/actuator`.

#### Scenario: Valid tuning applied

- **WHEN** `POST /api/control/tuning` is sent with all four fields in range and the CSRF header
- **THEN** the values SHALL be persisted and a gain change SHALL be requested of the control task

#### Scenario: Out-of-range field rejected wholesale

- **WHEN** a request carries a valid `kp` and a `ki` above its permitted maximum
- **THEN** the endpoint SHALL respond with an error naming `ki`, and neither field SHALL be stored

#### Scenario: Values are not clamped

- **WHEN** a request carries a field outside its range
- **THEN** the response SHALL be an error rather than a success reporting a clamped value

#### Scenario: Missing field rejected

- **WHEN** a request omits one of the four fields
- **THEN** the endpoint SHALL respond with an error and apply nothing

#### Scenario: Zero proportional gain rejected

- **WHEN** a request carries `kp` of zero
- **THEN** the endpoint SHALL respond with an error and apply nothing

#### Scenario: Missing CSRF header

- **WHEN** the request is sent without the CSRF header
- **THEN** it SHALL be rejected and no state SHALL change

#### Scenario: Deeper path is not shadowed

- **WHEN** `POST /api/control/tuning` is requested
- **THEN** it SHALL reach the tuning handler rather than a handler registered for `/api/control`

## MODIFIED Requirements

### Requirement: Control parameters endpoint

The firmware SHALL expose `GET /api/control` returning the temperature controller's live state and its tuning parameters in a single response: whether control is enabled, whether the PID is currently running, the setpoint, the current temperature, the control error, the computed output, the integral accumulator, the gains `kp`/`ki`/`kd`, the control interval, and the output clamp range.

The gains reported SHALL be the gains in force in the running controller, not the gains stored in configuration, so that a pending change which the control task has not yet consumed is not reported as already applied.

The control error SHALL be computed by the firmware as `setpoint − temperature` rather than left to the client, so the reported sign convention cannot disagree with the controller's. Fields derived from a temperature reading SHALL be omitted when no valid reading exists, matching how `/api/status` already omits `temperature`.

These fields SHALL NOT be added to `GET /api/status`, which is polled on a timer by every client; they are diagnostic detail to be fetched only when requested.

#### Scenario: Reporting control state

- **WHEN** `GET /api/control` is requested while control is enabled and sensor data is valid
- **THEN** the response SHALL include the enabled flag, running flag, setpoint, temperature, error, output, integral, gains, control interval and output range

#### Scenario: No valid temperature

- **WHEN** `GET /api/control` is requested while no valid temperature reading exists
- **THEN** the temperature and error fields SHALL be omitted
- **AND** the remaining fields SHALL still be reported

#### Scenario: Status endpoint is unchanged

- **WHEN** `GET /api/status` is requested
- **THEN** it SHALL NOT gain the gains or the integral accumulator

#### Scenario: A pending gain change is not reported as applied

- **WHEN** tuning has been written but the control task has not yet consumed the change
- **THEN** `GET /api/control` SHALL report the gains still in force

### Requirement: Autotune endpoints

The firmware SHALL expose `POST /api/autotune/start`, `POST /api/autotune/abort`, `POST /api/autotune/accept` and `GET /api/autotune/status`. The three POST endpoints SHALL require the `X-Requested-With: KlimaControl` CSRF header; the status read SHALL NOT.

`GET /api/autotune/status` SHALL report the run state, the abort reason when aborted, elapsed time, completed cycles, and — once converged — the identified `ku` and `tu` alongside the derived gains and the gains currently in force. It SHALL be sufficient on its own to reconstruct the full view after a page reload, because a run outlasts any particular browser session.

`POST /api/autotune/accept` SHALL persist the derived gains and request that the control task apply them. Because application happens on a later control tick, a success response SHALL mean the result was validated and the request accepted, not that the running controller has already adopted the gains. The gains in force SHALL be readable from `GET /api/control` and `GET /api/autotune/status`, which is how a caller confirms the outcome.

#### Scenario: Starting a run

- **WHEN** `POST /api/autotune/start` is sent while the autotuner is idle and control is enabled
- **THEN** the request SHALL be accepted and a run SHALL begin on a subsequent control tick

#### Scenario: Starting while control is disabled

- **WHEN** `POST /api/autotune/start` is sent while temperature control is disabled
- **THEN** the endpoint SHALL respond with HTTP 409 and no run SHALL begin

#### Scenario: Starting while a run is active

- **WHEN** `POST /api/autotune/start` is sent while a run is settling or oscillating
- **THEN** the endpoint SHALL respond with HTTP 409 and the existing run SHALL continue undisturbed

#### Scenario: Aborting

- **WHEN** `POST /api/autotune/abort` is sent during a run
- **THEN** the run SHALL be cancelled and the output SHALL return to zero

#### Scenario: Status is self-sufficient

- **WHEN** `GET /api/autotune/status` is requested at any point
- **THEN** the response SHALL carry enough to render the current state, progress and outcome without reference to earlier responses

#### Scenario: Status reports the abort reason

- **WHEN** a run has aborted
- **THEN** the status SHALL include a machine-readable reason

#### Scenario: Accepting a result

- **WHEN** `POST /api/autotune/accept` is sent while a converged result exists
- **THEN** the derived gains SHALL be persisted and a gain change SHALL be requested of the control task

#### Scenario: Acceptance is confirmed by reading back

- **WHEN** acceptance has succeeded and a control tick has elapsed
- **THEN** the gains in force reported by `GET /api/control` SHALL be the derived gains

#### Scenario: Accepting with no result

- **WHEN** `POST /api/autotune/accept` is sent when the state is not converged
- **THEN** the endpoint SHALL respond with HTTP 409 and the gains SHALL be unchanged

#### Scenario: Missing CSRF header

- **WHEN** any autotune POST is sent without the CSRF header
- **THEN** it SHALL be rejected and no state SHALL change