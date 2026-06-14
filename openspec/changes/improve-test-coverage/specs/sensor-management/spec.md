# sensor-management Specification

## MODIFIED Requirements

### Requirement: SensorController aggregation

`SensorController` SHALL own all sensor instances, expose `addSensor(std::unique_ptr<Sensor::Sensor>)`, `readSensors()`, `getMeasurements()`, `getTemperature()`, `getRelativeHumidity()`, `getDewPoint()`, `hasConnectedSensors()`, and `isDataValid()`. When multiple sensors of the same measurement type are present, the controller SHALL average their values.

#### Scenario: Two temperature sensors

- **WHEN** two `SHT4x` sensors report temperatures `T1` and `T2`
- **THEN** `getTemperature()` SHALL return `(T1 + T2) / 2`

#### Scenario: No connected sensors

- **WHEN** no sensors are present or all sensors are in `InitFailed` state
- **THEN** `hasConnectedSensors()` SHALL return false and `isDataValid()` SHALL return false

#### Scenario: Three sensor averaging

- **WHEN** three sensors report temperatures T1, T2, T3
- **THEN** `getTemperature()` SHALL return `(T1 + T2 + T3) / 3`

#### Scenario: Invalid readings excluded from average

- **WHEN** one of three sensors returns invalid reading
- **THEN** `getTemperature()` SHALL return average of valid readings only

#### Scenario: Snapshot consistency

- **WHEN** `getSnapshot()` is called while sensor task is updating
- **THEN** returned snapshot SHALL contain a consistent set of measurements (all from same reading cycle)

#### Scenario: getValidMeasurements returns empty on invalid data

- **WHEN** `isDataValid()` is false
- **THEN** `getValidMeasurements()` SHALL return empty vector