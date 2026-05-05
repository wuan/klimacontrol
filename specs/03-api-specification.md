# API Specification

## Purpose

Define all REST API endpoints, request/response formats, error handling, and status codes.

## Requirements

### General
1. **S3.1** SHALL use ESPAsyncWebServer for HTTP handling
2. **S3.2** SHALL return JSON responses with `application/json` content type
3. **S3.3** SHALL use ArduinoJson for JSON serialization

### Status Endpoints
4. **S3.4** SHALL provide `GET /api/status` - Device status overview
5. **S3.5** `GET /api/status` SHALL return: device_id, device_name, firmware_version, sensor_connected, sensor_valid, temperature, relative_humidity, dew_point, sensor_timestamp, target_temperature, control_enabled, wifi_connected, ip_address, wifi_ssid
6. **S3.6** SHALL provide `GET /api/about` - Detailed device info
7. **S3.7** SHALL provide `GET /api/measurements` - Detailed measurement table

### Sensor Endpoints
8. **S3.8** SHALL provide `GET /api/sensors` - List all sensors with status
9. **S3.9** SHALL provide `GET /api/sensors/config` - Get sensor configuration
10. **S3.10** SHALL provide `POST /api/sensors/config` - Update sensor configuration
11. **S3.11** SHALL provide `GET /api/sensors/registry` - Get known sensor types

### Temperature Control Endpoints
12. **S3.12** SHALL provide `POST /api/temperature/target` - Set target temperature
13. **S3.13** SHALL provide `POST /api/control/enable` - Enable temperature control
14. **S3.14** SHALL provide `POST /api/control/disable` - Disable temperature control

### Settings Endpoints
15. **S3.15** SHALL provide `POST /api/settings/wifi` - Update WiFi credentials
16. **S3.16** SHALL provide `POST /api/settings/device-name` - Update device name
17. **S3.17** SHALL provide `POST /api/settings/elevation` - Update elevation
18. **S3.18** SHALL provide `POST /api/settings/reboot` - Reboot device
19. **S3.19** SHALL provide `POST /api/settings/factory-reset` - Factory reset
20. **S3.20** SHALL provide `POST /api/restart` - Immediate restart

### OTA Endpoints
21. **S3.21** SHALL provide `GET /api/ota/check` - Check for updates
22. **S3.22** SHALL provide `POST /api/ota/update` - Perform OTA update
23. **S3.23** SHALL provide `GET /api/ota/status` - OTA status
24. **S3.24** SHALL provide `POST /api/ota/confirm` - Confirm boot

### MQTT Endpoints
25. **S3.25** SHALL provide `GET /api/mqtt` - Get MQTT config
26. **S3.26** SHALL provide `POST /api/mqtt` - Update MQTT config
27. **S3.27** SHALL provide `POST /api/mqtt/enable` - Enable MQTT
28. **S3.28** SHALL provide `POST /api/mqtt/disable` - Disable MQTT

### Page Routes
29. **S3.29** SHALL provide `GET /` - Main dashboard
30. **S3.30** SHALL provide `GET /config` - WiFi configuration page

## Scenarios

### Scenario S3-A: Getting Device Status
Given device is operational,
When GET /api/status is requested,
Then response SHALL include: temperature, humidity, target_temperature, control_enabled, wifi status

### Scenario S3-B: Updating Target Temperature
Given current target is 22.0°C,
When POST /api/temperature/target with `{"value": 23.5}`,
Then sensorController.setTargetTemperature(23.5) SHALL be called

### Scenario S3-C: Sensor Configuration Update
Given POST /api/sensors/config with devices array,
Then configuration SHALL be saved and device SHALL restart

### Scenario S3-D: Factory Reset
Given POST /api/settings/factory-reset,
Then config.reset() SHALL be called and device SHALL restart
