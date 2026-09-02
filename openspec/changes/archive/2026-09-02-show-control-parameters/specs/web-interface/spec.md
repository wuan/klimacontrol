# web-interface Specification Delta

## ADDED Requirements

### Requirement: Control parameters panel

The dashboard SHALL provide a collapsible panel showing the controller's live state and tuning parameters, backed by `GET /api/control`. It SHALL be fetched on demand rather than on the dashboard's polling timer, following the existing "Show Measurements" pattern, so that a client which is not looking at it costs the device nothing.

The panel SHALL present the controller output as a percentage of its clamp range, alongside a visual indication of magnitude. A percentage SHALL be shown rather than only the existing three-state symbol, because that symbol reports whether demand is non-zero and is therefore identical at 1 % and at 100 % demand.

The existing symbol SHALL be retained, as a glanceable summary that the e-paper footer mirrors.

#### Scenario: Panel is collapsed by default

- **WHEN** the dashboard first loads
- **THEN** the control parameters panel SHALL be hidden and `GET /api/control` SHALL NOT have been requested

#### Scenario: Showing the panel

- **WHEN** the user opens the panel
- **THEN** `GET /api/control` SHALL be requested and the values rendered

#### Scenario: Panel refreshes while visible

- **WHEN** the panel is open
- **THEN** it SHALL refresh on the dashboard's existing polling cadence
- **AND** SHALL stop refreshing once hidden

#### Scenario: Demand shown as a percentage

- **WHEN** the controller output is `0.42` with a clamp range of `0.0` to `1.0`
- **THEN** the panel SHALL show `42 %`

#### Scenario: Missing temperature is handled

- **WHEN** the response omits temperature and error because no valid reading exists
- **THEN** the panel SHALL render placeholders for those rows rather than `undefined`

#### Scenario: Symbol retained

- **WHEN** the panel is open
- **THEN** the three-state control symbol SHALL still be shown in the control bar
