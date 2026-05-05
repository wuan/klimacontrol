# Configuration Specification

## Purpose

Define persistent configuration storage using ESP32 Preferences (NVS).

## Requirements

### NVS Namespace
1. **S8.1** SHALL use namespace "klima" for all Preferences storage

### Configuration Manager
2. **S8.2** SHALL provide ConfigManager singleton class
3. **S8.3** SHALL provide begin() method for initialization
4. **S8.4** SHALL cache DeviceConfig in memory

### Configuration Structures
5. **S8.5** SHALL define WiFiConfig: ssid, password, configured, connection_failures
6. **S8.6** SHALL define DeviceConfig: device_id, device_name, sensor_i2c_address, target_temperature, temperature_control_enabled, elevation
7. **S8.7** SHALL define MqttConfig: host, port, username, password, prefix, interval, enabled
8. **S8.8** SHALL define SensorConfig: assignments string
9. **S8.9** SHALL define EnergyConfig: wifi_power
10. **S8.10** SHALL define SyslogConfig: host, port, enabled

### Partial Updates
11. **S8.11** SHALL provide updateDeviceName(const char*)
12. **S8.12** SHALL provide updateTargetTemperature(float)
13. **S8.13** SHALL provide updateTemperatureControlEnabled(bool)
14. **S8.14** SHALL provide updateElevation(float)
15. **S8.15** SHALL provide updateSensorI2CAddress(uint8_t)

### Device ID Generation
16. **S8.16** SHALL provide getDeviceId()
17. **S8.17** SHALL generate from last 3 bytes of MAC address
18. **S8.18** Format SHALL be uppercase hex without separators

### Connection Failure Tracking
19. **S8.19** SHALL provide incrementConnectionFailures()
20. **S8.20** SHALL provide resetConnectionFailures()
21. **S8.21** SHALL provide getConnectionFailures()

### Restart Management
22. **S8.22** SHALL provide requestRestart(uint32_t delayMs)
23. **S8.23** SHALL provide isRestartPending()
24. **S8.24** SHALL provide checkRestart()

### Validation
25. **S8.25** SHALL provide validateDeviceConfig()
26. **S8.26** SHALL check temperature range: -40.0 to 125.0
27. **S8.27** SHALL check elevation range: -100.0 to 10000.0
28. **S8.28** SHALL check sensor_i2c_address range: 0x08 to 0x77

### Factory Reset
29. **S8.29** SHALL provide reset()
30. **S8.30** SHALL clear all NVS data in "klima" namespace
31. **S8.31** SHALL clear in-memory cache

## Scenarios

### Scenario S8-A: Initial Load
Given first boot with empty NVS,
When begin() is called,
Then all configs SHALL load with default values

### Scenario S8-B: Generating Device ID
Given device_id is empty,
When getDeviceId() is called,
Then MAC address SHALL be used to generate ID (e.g., "D3E4F5")

### Scenario S8-C: Partial Update
Given target_temperature is 22.0,
When updateTargetTemperature(23.5) is called,
Then cache and NVS SHALL be updated

### Scenario S8-D: Connection Failures
Given current count is 2,
When incrementConnectionFailures() is called,
Then count SHALL be 3 and AP mode SHALL be entered

### Scenario S8-E: Factory Reset
Given device has custom config,
When reset() is called,
Then all NVS data SHALL be cleared and device SHALL restart
