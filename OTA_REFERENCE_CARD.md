# OTA Reference Card - Keep This Handy

## One-Line Examples

```cpp
// Check for updates from GitHub
FirmwareInfo info;
if (OTAUpdater::checkForUpdate("YOUR_USER", "untitled", info)) { /* Found update */ }

// Download and flash firmware
OTAUpdater::performUpdate(info.downloadUrl, info.size);

// Confirm successful boot
OTAUpdater::confirmBoot();

// Check memory before OTA
if (!OTAUpdater::hasEnoughMemory()) { return; }

// Get diagnostics
uint32_t free, min, psram;
OTAUpdater::getMemoryInfo(free, min, psram);
```

## API Cheat Sheet

| Method | Parameters | Returns | Purpose |
|--------|-----------|---------|---------|
| `checkForUpdate()` | owner, repo, info | bool | Find latest release on GitHub |
| `performUpdate()` | url, size, callback | bool | Download and flash firmware |
| `confirmBoot()` | none | bool | Mark OTA as valid/confirmed |
| `hasUnconfirmedUpdate()` | none | bool | Check if running NEW firmware |
| `hasEnoughMemory()` | none | bool | Verify 64KB+ free heap |
| `getMemoryInfo()` | free, min, psram | void | Get memory statistics |
| `getRunningPartitionInfo()` | label, address | bool | Get current partition info |

## Configuration

```cpp
// In OTAConfig.h
#define OTA_GITHUB_OWNER "your-username"
#define OTA_GITHUB_REPO "untitled"
#define FIRMWARE_VERSION "v1.0.0"
#define OTA_HTTP_TIMEOUT_MS 300000      // 5 minutes
#define OTA_DOWNLOAD_CHUNK_SIZE 4096    // 4KB chunks
```

## GitHub Release Creation

```bash
# Build firmware
pio run -e adafruit_qtpy_esp32s3_nopsram

# Create release
gh release create v1.0.0 \
  .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin
```

## Error Codes & Solutions

| Error Message | Cause | Fix |
|---------------|-------|-----|
| `checkForUpdate() returns false` | No .bin in release | Add firmware.bin to assets |
| `performUpdate() timeout` | Slow network | Increase `OTA_HTTP_TIMEOUT_MS` |
| `Insufficient memory` | Need 64KB+ free | Pause background tasks |
| `Device boots old firmware` | OTA not confirmed | Call `confirmBoot()` |
| `HTTP error 416` | Resume issue | Already fixed (HTTP/1.0) |

## Partition Table

Your 4MB device has:
- **app0** (1,856 KB) - Running firmware
- **app1** (1,856 KB) - Update target
- **otadata** (8 KB) - Rollback control
- **nvs** (20 KB) - Configuration
- **spiffs** (256 KB) - Web files
- **coredump** (64 KB) - Crash logs

## Memory Budget

```
Total Available: ~320 KB SRAM
├─ Framework: 50 KB
├─ WiFi: 70 KB
├─ HTTPClient: 25 KB
├─ Update.h: 10 KB
└─ Free for App: ~150 KB ✓
```

Minimum required for OTA: **64 KB free heap**
Your device has: **5x this amount**

## Security Checklist

- ✓ Uses HTTPS with certificate verification
- ✓ CA certificate bundle included (esp_crt_bundle)
- ✓ Protects against MITM attacks
- ✓ No time zone requirement
- ✓ Works offline after firmware cached

## Rollback Mechanism

```
New firmware boots:
├─ Marked as NEW
├─ If confirmBoot() called → Marked VALID
└─ If NOT called → Auto-rollback on next boot
```

## Web API Endpoints (Example)

```
GET /api/ota/check
  → Returns: { "version": "v1.0.0", "size": 1024000, "url": "..." }

POST /api/ota/update?url=...&size=1024000
  → Starts update, returns 202 Accepted
```

## Performance Metrics

| Metric | Value | Notes |
|--------|-------|-------|
| Check Release | 1-2 sec | GitHub API call |
| Download 988KB | 30-120 sec | Depends on WiFi (80-330 KB/s) |
| Flash 988KB | 1-2 sec | Direct SPI flash write |
| Boot New | 5-10 sec | ESP32-S3 startup |
| Total Update Time | 40-130 sec | Network dependent |

## Typical Update Flow

```
Time    Event
────    ──────────────────────────────
0 sec   User clicks "Update"
1 sec   checkForUpdate() completes
2 sec   performUpdate() starts
30 sec  50% downloaded
60 sec  100% downloaded, flashing complete
61 sec  Device reboot initiated
65 sec  New firmware boots
70 sec  Firmware verification complete
71 sec  confirmBoot() called
≈2 min  Update complete
```

## Files to Keep

Keep these files in your project:
- `src/OTAUpdater.h`
- `src/OTAUpdater.cpp`
- `src/OTAConfig.h`

Read these documentation files:
- `docs/OTA_QUICK_START.md` (5 min)
- `docs/OTA_FIRMWARE_UPDATES.md` (detailed)
- `docs/OTA_INDEX.md` (navigation)

Reference these examples:
- `examples/OTA_INTEGRATION_EXAMPLE.cpp`
- `partitions.csv` (if you have 4MB flash)

## Common Integration Points

### In setup()
```cpp
if (OTAUpdater::hasUnconfirmedUpdate()) {
  OTAUpdater::confirmBoot();
}
```

### In loop() or task
```cpp
if (WiFi.status() == WL_CONNECTED) {
  FirmwareInfo info;
  if (OTAUpdater::checkForUpdate("user", "repo", info)) {
    OTAUpdater::performUpdate(info.downloadUrl, info.size);
    ESP.restart();
  }
}
```

### In WebServerManager
```cpp
server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request) {
  // Handle check endpoint
});
server.on("/api/ota/update", HTTP_POST, [](AsyncWebServerRequest *request) {
  // Handle update endpoint
});
```

## Quick Troubleshooting Tree

```
Update fails?
├─ checkForUpdate() returns false?
│  ├─ No internet? → Check WiFi
│  └─ No .bin in release? → Upload firmware.bin to GitHub
├─ performUpdate() returns false?
│  ├─ Timeout? → Check WiFi speed, increase timeout
│  ├─ Out of memory? → Pause other tasks
│  └─ HTTP error? → Check GitHub URL
└─ Device boots old firmware?
   └─ Call confirmBoot() after verification

Device won't compile?
├─ Can't find OTAUpdater.h? → Check include path
├─ WiFiClientSecure error? → Update Arduino ESP32 core
└─ ArduinoJson error? → Already in your dependencies
```

## Remember

1. **Always** update `OTA_GITHUB_OWNER` in OTAConfig.h
2. **Always** create GitHub releases with `.bin` files
3. **Always** verify memory before OTA (call `hasEnoughMemory()`)
4. **Always** call `confirmBoot()` after verification
5. **Never** skip the GitHub certificate verification
6. **Never** remove the OTA data partition
7. **Never** modify app0 while running (it's the active partition)

## Version Info

- Created: 2026-01-05
- Device: Adafruit QT Py ESP32-S3 (No PSRAM, 2MB Flash)
- Framework: Arduino
- ArduinoJson: 6.21.3+
- ESP32 Core: Latest stable

## Need More Help?

1. **Quick start**: `docs/OTA_QUICK_START.md`
2. **API reference**: `src/OTAUpdater.h`
3. **Examples**: `examples/OTA_INTEGRATION_EXAMPLE.cpp`
4. **Deep dive**: `docs/OTA_FIRMWARE_UPDATES.md`
5. **Navigation**: `docs/OTA_INDEX.md`

**Happy updating! 🚀**
