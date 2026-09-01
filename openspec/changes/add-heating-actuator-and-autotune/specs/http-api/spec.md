# http-api Specification Delta

## ADDED Requirements

### Requirement: Tuning endpoints

The firmware SHALL expose `GET /api/tuning` returning the current gains, cycle period, control interval and safety limits, and `POST /api/tuning` accepting any subset of those fields. Every field SHALL be validated against its documented range and out-of-range or non-finite values SHALL be rejected with HTTP 400, leaving all stored values unchanged. A partial update SHALL NOT be applied when any field in the request is invalid, so a rejected request never leaves the controller half-reconfigured. `POST /api/tuning` SHALL require the `X-Requested-With: KlimaControl` CSRF header.

#### Scenario: Reading current tuning

- **WHEN** `GET /api/tuning` is requested
- **THEN** the response SHALL contain the current `kp`, `ki`, `kd`, cycle period, control interval and safety limits

#### Scenario: Updating a single gain

- **WHEN** `POST /api/tuning` is sent with `{"kp": 1.2}`
- **THEN** `Kp` SHALL be updated and persisted, and the remaining fields SHALL be unchanged

#### Scenario: Invalid field rejects the whole request

- **WHEN** `POST /api/tuning` is sent with one valid and one out-of-range field
- **THEN** the endpoint SHALL respond with HTTP 400 and neither field SHALL be applied

#### Scenario: Missing CSRF header

- **WHEN** `POST /api/tuning` is called without `X-Requested-With: KlimaControl`
- **THEN** the request SHALL be rejected and no state SHALL change

### Requirement: Autotune endpoints

The firmware SHALL expose `POST /api/autotune/start` (optionally carrying an auto-apply flag), `POST /api/autotune/abort`, `POST /api/autotune/accept` to adopt proposed gains, and `GET /api/autotune/status`. The status response SHALL report the state, elapsed time, completed cycle count, the most recent measured amplitude and period, and — once converged — the derived gains alongside the current ones. All three POST endpoints SHALL require the CSRF header.

Because a run lasts hours, the status endpoint SHALL be sufficient to reconstruct the full view after a browser reload; no run state SHALL live only in the page.

#### Scenario: Starting a run

- **WHEN** `POST /api/autotune/start` is sent while the autotuner is `Idle` and control is enabled
- **THEN** the run SHALL enter `Settling` and `GET /api/autotune/status` SHALL report it

#### Scenario: Starting while already running

- **WHEN** `POST /api/autotune/start` is sent while a run is active
- **THEN** the endpoint SHALL respond with HTTP 409 and the existing run SHALL continue undisturbed

#### Scenario: Starting with control disabled

- **WHEN** `POST /api/autotune/start` is sent while `temperature_control_enabled` is `false`
- **THEN** the endpoint SHALL respond with HTTP 409 and no run SHALL start

#### Scenario: Aborting a run

- **WHEN** `POST /api/autotune/abort` is sent during a run
- **THEN** the run SHALL move to `Aborted`, the output SHALL be switched off, and the stored gains SHALL remain unchanged

#### Scenario: Status survives a browser reload

- **WHEN** the page is reloaded four hours into a run
- **THEN** `GET /api/autotune/status` SHALL report the state, elapsed time and cycles completed so far

#### Scenario: Accepting proposed gains

- **WHEN** `POST /api/autotune/accept` is sent while the state is `Done`
- **THEN** the derived gains SHALL be persisted and adopted

#### Scenario: Accepting when no proposal exists

- **WHEN** `POST /api/autotune/accept` is sent while the state is not `Done`
- **THEN** the endpoint SHALL respond with HTTP 409 and no gains SHALL change

## MODIFIED Requirements

### Requirement: Status endpoints

`GET /api/status` SHALL additionally report the valve state and, when a run is active, a compact autotune indicator, so that a client polling only this endpoint can render the dashboard without also polling the autotune endpoint.

#### Scenario: Valve state reported

- **WHEN** `GET /api/status` is requested
- **THEN** the response SHALL include whether the valve is currently commanded open

#### Scenario: Autotune indicated on the dashboard

- **WHEN** an autotune run is active
- **THEN** `GET /api/status` SHALL indicate that the normal controller is not in charge of the output