# http-api Specification Delta

## MODIFIED Requirements

### Requirement: Temperature control endpoints

The firmware SHALL expose: `POST /api/temperature/target` (set setpoint),
`POST /api/control/enable`, `POST /api/control/disable`.

`POST /api/temperature/target` SHALL validate the requested value **before**
applying it and SHALL reject any value that is not a finite number within
`[10.0, 30.0]` °C. A rejected request SHALL leave `DeviceConfig.target_temperature`
unchanged, SHALL perform no NVS write, and SHALL respond with HTTP 400 and a
JSON body carrying `"success": false`. The endpoint SHALL NOT silently clamp an
out-of-range value and report success.

#### Scenario: Setting target temperature

- **WHEN** `POST /api/temperature/target` is sent with body `{"value": 23.5}`
- **THEN** `sensorController.setTargetTemperature(23.5)` SHALL be invoked and
  the new value SHALL be persisted to `DeviceConfig.target_temperature`

#### Scenario: Boundary values are accepted

- **WHEN** `POST /api/temperature/target` is sent with `{"value": 10.0}` or
  `{"value": 30.0}`
- **THEN** the request SHALL be accepted and the setpoint SHALL be persisted

#### Scenario: Out-of-range setpoint

- **WHEN** the request body contains a value outside the validated range
  (`10.0` … `30.0` °C)
- **THEN** the endpoint SHALL respond with HTTP 400
- **AND** the controller setpoint SHALL remain unchanged
- **AND** no value SHALL be written to NVS

#### Scenario: Non-finite setpoint

- **WHEN** the request body contains a value that is not a finite number
- **THEN** the endpoint SHALL respond with HTTP 400 and the setpoint SHALL
  remain unchanged

#### Scenario: Missing CSRF header

- **WHEN** any of the three control endpoints is called without
  `X-Requested-With: KlimaControl`
- **THEN** the request SHALL be rejected by `verifyCsrfHeader()` and SHALL have
  no effect on device state