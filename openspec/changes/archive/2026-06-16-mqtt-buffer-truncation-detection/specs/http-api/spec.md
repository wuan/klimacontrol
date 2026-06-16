## MODIFIED Requirements

### Requirement: MQTT endpoints

The firmware SHALL expose: `GET /api/mqtt`, `POST /api/mqtt`, `POST /api/mqtt/enable`, `POST /api/mqtt/disable`.

#### Scenario: Reading MQTT configuration

- **WHEN** `GET /api/mqtt` is requested
- **THEN** the response SHALL include `host`, `port`, `username`, `prefix`, `interval`, `enabled` (passwords SHALL NOT be echoed back), `buffer_size`, `buffer_degraded`, and `truncated_publishes` (the buffer fields are documented in the mqtt-integration spec, "MQTT TX buffer state is observable" requirement)
