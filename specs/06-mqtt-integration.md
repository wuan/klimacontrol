# MQTT Integration Specification

## Purpose

Define MQTT client for sensor data publishing.

## Requirements

### Configuration
1. **S6.1** SHALL store MqttConfig: host, port, username, password, prefix, interval, enabled
2. **S6.2** Default port SHALL be 1883
3. **S6.3** Default prefix SHALL be "sensors"
4. **S6.4** Default interval SHALL be 15 seconds

### MQTT Client
5. **S6.5** SHALL use PubSubClient library
6. **S6.6** SHALL set client ID to device_id
7. **S6.7** SHALL support QoS levels 0 and 1

### Connection Management
8. **S6.8** SHALL attempt connection on startup if enabled
9. **S6.9** SHALL implement automatic reconnection
10. **S6.10** SHALL use exponential backoff (max 5 minutes)

### Message Publishing
11. **S6.11** SHALL publish at configured interval
12. **S6.12** Topic format SHALL be: `{prefix}/{device_id}/{measurement_type}`
13. **S6.13** SHALL publish temperature, humidity, dew_point, pressure, etc.
14. **S6.14** Message SHALL be JSON with value, unit, timestamp, device_id

### LWT
15. **S6.15** SHALL support Last Will and Testament
16. **S6.16** LWT topic SHALL be `{prefix}/{device_id}/status`
17. **S6.17** LWT message SHALL be "offline"

## Scenarios

### Scenario S6-A: Publishing Sensor Data
Given MQTT enabled, device_id="bedroom", prefix="home/sensors",
When interval elapses,
Then temperature SHALL be published to home/sensors/bedroom/temperature

### Scenario S6-B: Connection Failure and Reconnection
Given connection is lost,
Then device SHALL attempt reconnection with exponential backoff

### Scenario S6-C: LWT on Unclean Disconnect
Given device crashes,
Then broker SHALL publish "offline" to LWT topic
