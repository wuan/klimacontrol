# Web Interface Specification

## Purpose

Define web-based user interface, HTML/CSS/JS structure, pages, and real-time updates.

## Requirements

### Architecture
1. **S7.1** SHALL use ESPAsyncWebServer
2. **S7.2** SHALL embed HTML/CSS/JS as C++ string literals
3. **S7.3** SHALL NOT use filesystem for web assets

### Pages
4. **S7.4** SHALL provide main dashboard at /
5. **S7.5** SHALL provide settings page at /settings

### Dashboard
6. **S7.6** SHALL display current temperature and humidity
7. **S7.7** SHALL display target temperature
8. **S7.8** SHALL display control status (enabled/disabled)
9. **S7.9** SHALL display sensor status
10. **S7.10** SHALL display WiFi status

### Temperature Control UI
11. **S7.11** SHALL provide +/- buttons for target temperature
12. **S7.12** SHALL provide toggle for control enable/disable
13. **S7.13** Increment SHALL be 0.5°C per button press

### Real-time Updates
14. **S7.14** SHALL use polling (no WebSockets)
15. **S7.15** SHALL poll /api/status every 2 seconds
16. **S7.16** SHALL update UI without page refresh

### JavaScript
17. **S7.17** SHALL use vanilla JavaScript (no frameworks)
18. **S7.18** SHALL use fetch API for HTTP requests
19. **S7.19** SHALL provide toast notifications
20. **S7.20** SHALL provide confirmation dialogs for destructive actions

### Settings
21. **S7.21** SHALL provide settings modal with tabs
22. **S7.22** Tabs SHALL include: Device, WiFi, MQTT, OTA, System

## Scenarios

### Scenario S7-A: Loading Dashboard
Given device operational,
When user navigates to device IP,
Then dashboard SHALL display temperature, humidity, target, control status, WiFi info

### Scenario S7-B: Adjusting Target Temperature
Given target is 22.0°C,
When user clicks + twice,
Then POST /api/temperature/target with {"value": 23.0} SHALL be sent

### Scenario S7-C: WiFi Configuration
Given device in AP mode,
When user submits SSID and password,
Then POST /api/settings/wifi SHALL be sent and device SHALL restart

### Scenario S7-D: Factory Reset
Given user confirms factory reset,
Then POST /api/settings/factory-reset SHALL be sent and device SHALL restart

### Scenario S7-E: OTA Update
Given update available,
When user clicks Install Update,
Then POST /api/ota/update SHALL be sent, progress shown, device restarts
