# OTA Updates Specification

## Purpose

Define over-the-air firmware update mechanism.

## Requirements

### GitHub Integration
1. **S5.1** SHALL use GitHub REST API for releases
2. **S5.2** SHALL query latest release from owner/repo
3. **S5.3** SHALL compare current FIRMWARE_VERSION with latest

### Memory Safety
4. **S5.4** SHALL require minimum 64 KB free heap
5. **S5.5** SHALL provide hasEnoughMemory() method

### Firmware Download
6. **S5.6** SHALL provide performUpdate() method
7. **S5.7** SHALL use HTTPS with esp_crt_bundle for certificate verification
8. **S5.8** SHALL stream firmware to inactive partition in 4 KB chunks
9. **S5.9** SHALL verify download size matches expected
10. **S5.10** SHALL provide progress callbacks

### Partition Management
11. **S5.11** SHALL use ESP32 OTA partition API
12. **S5.12** SHALL have partition layout: app0 (1,856 KB), app1 (1,856 KB), otadata (8 KB)
13. **S5.13** SHALL identify running and inactive partitions

### Rollback
14. **S5.14** SHALL support automatic rollback via otadata partition
15. **S5.15** SHALL provide hasUnconfirmedUpdate() method
16. **S5.16** SHALL provide confirmBoot() to disable rollback

### Configuration
17. **S5.17** OTAConfig.h SHALL define OTA_GITHUB_OWNER and OTA_GITHUB_REPO
18. **S5.18** Constants.h SHALL define FIRMWARE_VERSION

## Scenarios

### Scenario S5-A: Checking for Updates
Given v0.0.74 is available, current is v0.0.73,
When GET /api/ota/check,
Then response SHALL include update_available: true, latest_version: "v0.0.74"

### Scenario S5-B: Performing OTA Update
Given update available at URL with size N,
When POST /api/ota/update with download_url and size,
Then:
1. Immediate 200 response
2. Firmware SHALL be downloaded and flashed
3. On success, device SHALL restart

### Scenario S5-C: Automatic Rollback
Given OTA update fails to start properly,
Then device SHALL automatically rollback to previous firmware

### Scenario S5-D: Confirming Boot
Given new firmware runs successfully,
When POST /api/ota/confirm,
Then rollback SHALL be disabled for current partition
