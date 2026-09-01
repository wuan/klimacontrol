# http-api Specification Delta

## ADDED Requirements

### Requirement: Control active state in status endpoint

The `GET /api/status` endpoint SHALL include a `control_active` boolean field in its JSON response. The field SHALL be `true` when the temperature control is enabled and actively producing non-zero output, and `false` otherwise (including when control is disabled, when temperature is at setpoint, or when sensor data is invalid).

#### Scenario: Status includes control_active field

- **WHEN** a client sends `GET /api/status`
- **THEN** the response JSON SHALL include `"control_active": true` or `"control_active": false`

#### Scenario: Control active when heating

- **WHEN** temperature is below setpoint and control is enabled
- **THEN** `GET /api/status` response SHALL include `"control_active": true`

#### Scenario: Control not active at setpoint

- **WHEN** temperature equals setpoint and control is enabled
- **THEN** `GET /api/status` response SHALL include `"control_active": false`

#### Scenario: Control not active when disabled

- **WHEN** control is disabled
- **THEN** `GET /api/status` response SHALL include `"control_active": false` regardless of temperature

#### Scenario: Backwards compatibility with old clients

- **WHEN** an old client that does not expect `control_active` receives the response
- **THEN** the client SHALL continue to function correctly, as it will simply ignore the unknown field
