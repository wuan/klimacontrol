# Sensor Management Specification

## Purpose

Define how sensors are discovered, configured, initialized, read, managed, and how measurements are processed and combined.

## Requirements

### Sensor Types and Support

1. **S2.1** SHALL support SHT4x temperature/humidity sensor
2. **S2.2** SHALL support BME680 environmental sensor
3. **S2.3** SHALL support SGP40 VOC sensor
4. **S2.4** SHALL support BMP3xx pressure sensor
5. **S2.5** SHALL support SCD4x CO2 sensor
6. **S2.6** SHALL support TSL2591 light sensor
7. **S2.7** SHALL support PM25 particulate matter sensor
8. **S2.8** SHALL support VEML7700 ambient light sensor
9. **S2.9** SHALL support DPS310 pressure sensor
10. **S2.10** SHALL support BH1750 light sensor

### Sensor Configuration

11. **S2.11** SHALL store sensor assignments as compact string (e.g., "44=SHT4x,77=BME680")
12. **S2.12** SHALL parse assignment string on startup to instantiate sensors
13. **S2.13** SHALL support configuring sensor at any valid 7-bit I2C address (0x08-0x77)
14. **S2.14** Default sensor I2C address SHALL be 0x44
15. **S2.15** SHALL ignore unknown sensor types with warning log

### Sensor Interface

16. **S2.16** All sensors SHALL inherit from `Sensor::Sensor` base class
17. **S2.17** Each sensor SHALL implement `begin()` for initialization
18. **S2.18** Each sensor SHALL implement `read()` returning SensorReading
19. **S2.19** Each sensor SHALL implement `isConnected()` for connectivity check
20. **S2.20** Each sensor SHALL implement `getType()` returning sensor type string
21. **S2.21** Each sensor SHALL implement `getStatus()` returning SensorStatus enum

### Sensor Status Tracking

22. **S2.22** SensorStatus SHALL have values: Uninitialized, Online, InitFailed, ReadFailing
23. **S2.23** Sensor SHALL start in Uninitialized state
24. **S2.24** On successful `begin()`, sensor SHALL transition to Online state
25. **S2.25** On failed `begin()`, sensor SHALL transition to InitFailed state
26. **S2.26** On 10 consecutive read failures, sensor SHALL transition to ReadFailing state
27. **S2.27** READ_FAILURE_THRESHOLD SHALL be 10 consecutive failures

### Measurement Types

28. **S2.28** SHALL support MeasurementType enum
29. **S2.29** MeasurementType SHALL include: Temperature, RelativeHumidity, DewPoint, Pressure, SeaLevelPressure, GasResistance, CO2, Illuminance, Particles, PM concentrations, VocIndex
30. **S2.30** Each MeasurementType SHALL have associated label and unit strings
31. **S2.31** Measurement value SHALL be `std::variant<float, int32_t>`

### SensorController Responsibilities

32. **S2.32** SensorController SHALL manage all sensor instances
33. **S2.33** SensorController SHALL provide `addSensor(std::unique_ptr<Sensor::Sensor>)` method
34. **S2.34** SensorController SHALL provide `readSensors()` to read all sensors
35. **S2.35** SensorController SHALL average readings from multiple sensors of same type
36. **S2.36** SensorController SHALL provide `getTemperature()` returning first temperature or NAN
37. **S2.37** SensorController SHALL provide `getRelativeHumidity()` returning first humidity or NAN
38. **S2.38** SensorController SHALL provide `getDewPoint()` calculated from temperature and humidity
39. **S2.39** SensorController SHALL provide `hasConnectedSensors()` and `isDataValid()`

### Calculated Measurements

40. **S2.40** SHALL calculate dew point using Magnus formula: `a=17.625, b=243.04`
41. **S2.41** SHALL calculate sea-level pressure using hypsometric formula

## Scenarios

### Scenario S2-A: Sensor Initialization

Given configuration string "44=SHT4x,77=BME680",
When `setup()` parses and creates sensors,
Then:
1. String SHALL be split by commas
2. Each sensor SHALL be created with `std::make_unique`
3. Each sensor SHALL be added via `sensorController.addSensor(std::move(sensor))`
4. Each sensor SHALL call `tryBegin()` to initialize

### Scenario S2-B: Reading from Multiple Sensors

Given two SHT4x sensors at 0x44 and 0x45,
When `sensorController.readSensors()` is called,
Then:
1. Each sensor's `read()` method SHALL be called
2. SensorController SHALL collect and average measurements
3. `getTemperature()` SHALL return the averaged temperature

### Scenario S2-C: Sensor Failure Handling

Given a sensor that fails to read,
When sensor experiences 10 consecutive read failures,
Then:
1. After 10th failure, status SHALL change to ReadFailing
2. On next successful read, status SHALL change back to Online
