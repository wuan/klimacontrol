# web-interface Specification Delta

## ADDED Requirements

### Requirement: Autotune controls and status

The web UI SHALL provide a way to start an autotune run, abort a running one, view its progress, and accept a derived result. Progress SHALL be reconstructed from `GET /api/autotune/status` so that it survives a page reload, and an abort control SHALL be reachable whenever a run is active.

#### Scenario: Progress survives a reload

- **WHEN** the page is reloaded during a run
- **THEN** the state, elapsed time and completed cycles SHALL be shown, rebuilt from the status endpoint

#### Scenario: Abort reachable while running

- **WHEN** a run is active
- **THEN** an abort control SHALL be visible

#### Scenario: Abort reason surfaced

- **WHEN** a run aborts for any reason
- **THEN** the recorded reason SHALL be displayed rather than the UI returning silently to idle

### Requirement: Autotune UI states its current limitations

Because the device has no heating output, a run cannot converge: the plant does not respond to the relay, so every run ends in a timeout. The UI SHALL say so before a run is started, so that the expected outcome is not read as a malfunction.

The UI SHALL also state that an accepted result is applied in memory only and does not survive a restart, rather than presenting acceptance as a durable change.

#### Scenario: Limitation stated before starting

- **WHEN** the user views the autotune controls
- **THEN** the UI SHALL state that runs cannot converge until a heating output exists

#### Scenario: Timeout is not presented as a fault

- **WHEN** a run ends in a settling or run timeout
- **THEN** the UI SHALL report the reason without implying a malfunction

#### Scenario: Acceptance is described as temporary

- **WHEN** derived gains are offered for acceptance
- **THEN** the UI SHALL state that they are not persisted and are lost on restart
