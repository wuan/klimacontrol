# http-api Specification Delta

## ADDED Requirements

### Requirement: Autotune endpoints

The firmware SHALL expose `POST /api/autotune/start`, `POST /api/autotune/abort`, `POST /api/autotune/accept` and `GET /api/autotune/status`. The three POST endpoints SHALL require the `X-Requested-With: KlimaControl` CSRF header; the status read SHALL NOT.

`GET /api/autotune/status` SHALL report the run state, the abort reason when aborted, elapsed time, completed cycles, and — once converged — the identified `ku` and `tu` alongside the derived gains and the gains currently in force. It SHALL be sufficient on its own to reconstruct the full view after a page reload, because a run outlasts any particular browser session.

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
- **THEN** the derived gains SHALL be applied to the running controller

#### Scenario: Accepting with no result

- **WHEN** `POST /api/autotune/accept` is sent when the state is not converged
- **THEN** the endpoint SHALL respond with HTTP 409 and the gains SHALL be unchanged

#### Scenario: Missing CSRF header

- **WHEN** any autotune POST is sent without the CSRF header
- **THEN** it SHALL be rejected and no state SHALL change
