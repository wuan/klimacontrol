# ESP32-S3 OTA Quick Start Guide

## Minimum Integration (5 minutes)

### Step 1: Add to platformio.ini

Ensure these libraries are in your dependencies (already in your project):

```ini
lib_deps =
    bblanchon/ArduinoJson@^6.21.3
    # HTTPClient and Update.h are built-in
```

### Step 2: Add Files to Your Project

```
src/
├── OTAUpdater.h
└── OTAUpdater.cpp
```

### Step 3: One-Line Firmware Check and Update

```cpp
#include "OTAUpdater.h"

void setup() {
  // ... your setup code ...

  FirmwareInfo info;
  if (OTAUpdater::checkForUpdate("YOUR_GITHUB_USERNAME", "untitled", info)) {
    OTAUpdater::performUpdate(info.downloadUrl, info.size);
    ESP.restart();
  }
}
```

**That's it!** Your ESP32-S3 can now fetch and flash new firmware from GitHub releases.

---

## API Quick Reference

### Check for Updates

```cpp
FirmwareInfo info;
bool success = OTAUpdater::checkForUpdate(
  "github-username",     // Repository owner
  "repo-name",          // Repository name
  info                  // [out] Release details
);

if (success) {
  Serial.printf("Version: %s\n", info.version.c_str());
  Serial.printf("Size: %zu bytes\n", info.size);
  Serial.printf("URL: %s\n", info.downloadUrl.c_str());
  Serial.printf("Notes: %s\n", info.releaseNotes.c_str());
}
```

### Perform Update

```cpp
// Simple
bool success = OTAUpdater::performUpdate(downloadUrl, expectedSize);

// With progress callback
auto onProgress = [](int percent, size_t bytes) {
  Serial.printf("Progress: %d%% (%zu bytes)\n", percent, bytes);
};
bool success = OTAUpdater::performUpdate(downloadUrl, expectedSize, onProgress);
```

### Confirm Boot (for rollback safety)

```cpp
// Call this after verifying new firmware works
bool success = OTAUpdater::confirmBoot();
```

### Check Memory

```cpp
if (OTAUpdater::hasEnoughMemory()) {
  // Safe to perform OTA
}

uint32_t freeHeap, minFree, psramFree;
OTAUpdater::getMemoryInfo(freeHeap, minFree, psramFree);
```

---

## Partition Table Reference

Your current 4MB partition setup is OTA-ready:

```
nvs      (20K)    - Device configuration
otadata  (8K)     - OTA metadata & rollback control
app0     (1856K)  - Primary firmware partition
app1     (1856K)  - Secondary firmware partition (OTA target)
spiffs   (256K)   - Web assets
coredump (64K)    - Crash dumps
```

**Available for your firmware**: 1,856 KB per partition
**Your current size**: 988 KB
**Headroom**: 868 KB

---

## GitHub Release Setup

### Create Release with Firmware

```bash
# Build your project (generates .bin file)
platformio run -e adafruit_qtpy_esp32s3_nopsram

# The .bin file will be at:
# .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin

# Create release and upload
gh release create v1.0.0 \
  .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin
```

### Release Naming Convention

For the OTA updater to find your firmware:
- Release must have a `.bin` file (e.g., `firmware.bin`)
- Tag name format: `v1.0.0`, `v1.2.3`, etc.
- Release notes are optional but recommended

---

## Memory Analysis for Your Device

```
Total Flash: 2 MB
├── app0 + app1 partitions: 3.7 MB (in your case, device is 4MB)
├── NVS (config): 20 KB
├── SPIFFS (web): 256 KB
└── Coredump: 64 KB

SRAM During OTA:
├── Framework: 50 KB
├── WiFi: 70 KB
├── HTTPClient: 25 KB
├── Update.h: 10 KB
├── Download Buffer: 4 KB
└── Free for App: ~170 KB
```

**Minimum Required**: 64 KB free heap
**Recommended**: 128 KB free heap

---

## Common Scenarios

### Scenario 1: Automatic Daily Update Check

```cpp
TaskHandle_t otaTaskHandle = nullptr;

void otaTask(void* param) {
  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      FirmwareInfo info;
      if (OTAUpdater::checkForUpdate("username", "repo", info)) {
        if (OTAUpdater::performUpdate(info.downloadUrl, info.size)) {
          ESP.restart();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(24 * 60 * 60 * 1000));  // 24 hours
  }
}

void setup() {
  // ... other setup ...
  xTaskCreatePinnedToCore(otaTask, "OTA", 4096, nullptr, 1, &otaTaskHandle, 0);
}
```

### Scenario 2: Manual Update via Web API

```cpp
// Add to your web server endpoints
server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request) {
  FirmwareInfo info;
  if (OTAUpdater::checkForUpdate("username", "repo", info)) {
    DynamicJsonDocument doc(256);
    doc["version"] = info.version;
    doc["size"] = (uint32_t)info.size;
    doc["url"] = info.downloadUrl;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  } else {
    request->send(503, "application/json", "{\"error\":\"GitHub unavailable\"}");
  }
});

server.on("/api/ota/update", HTTP_POST, [](AsyncWebServerRequest *request) {
  String url = request->getParam("url")->value();
  size_t size = strtoul(request->getParam("size")->value().c_str(), nullptr, 10);

  request->send(202, "text/plain", "Starting update");

  if (OTAUpdater::performUpdate(url, size)) {
    delay(2000);
    ESP.restart();
  }
});
```

### Scenario 3: GPIO Button to Force OTA Mode

```cpp
void checkForceButton() {
  const int FORCE_PIN = 8;
  gpio_set_direction((gpio_num_t)FORCE_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)FORCE_PIN);

  delay(50);

  if (gpio_get_level((gpio_num_t)FORCE_PIN) == 0) {
    Serial.println("OTA mode enabled");
    // Disable LED task
    vTaskSuspend(ledTaskHandle);

    // Wait for web-based update
    while (true) {
      delay(1000);
    }
  }
}

// Call in setup() before other initialization
```

### Scenario 4: Safe Update with Verification

```cpp
bool updateWithVerification() {
  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate("user", "repo", info)) return false;

  // Perform update
  if (!OTAUpdater::performUpdate(info.downloadUrl, info.size)) return false;

  // Reboot
  delay(1000);
  ESP.restart();
}

void loop() {
  // After reboot, device runs new firmware automatically

  // Perform verification checks
  bool allGood = true;
  if (!checkNetworkConnectivity()) allGood = false;
  if (!checkLEDFunctionality()) allGood = false;
  if (!checkCriticalFeatures()) allGood = false;

  // Confirm OTA after 30 seconds of success
  static unsigned long verifyStart = millis();
  if (allGood && millis() - verifyStart > 30000) {
    OTAUpdater::confirmBoot();
    Serial.println("OTA confirmed");
  }
}
```

---

## Troubleshooting

| Problem | Cause | Solution |
|---------|-------|----------|
| "checkForUpdate() returns false" | No .bin file in release | Ensure release contains `firmware.bin` |
| "performUpdate() fails after 30s" | Slow network | Increase `TIMEOUT_MS` or reduce chunk size |
| "Stack overflow during download" | Large buffer | Use streaming (already done in OTAUpdater) |
| "Device boots old firmware" | OTA not confirmed | Call `confirmBoot()` after verification |
| "Insufficient memory" | Too many tasks running | Suspend non-essential tasks before OTA |
| "HTTP error 416" | Resume request issue | HTTP/1.0 already enabled (no change needed) |

---

## Testing the Implementation

### Test 1: Create a Test Release

```bash
# Build your firmware
pio run -e adafruit_qtpy_esp32s3_nopsram

# Create a test release on GitHub
gh release create v1.0.0 \
  .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin \
  --draft  # Mark as draft so it doesn't show publicly

# Test code
FirmwareInfo info;
OTAUpdater::checkForUpdate("your-user", "untitled", info);
// Should find and parse your release
```

### Test 2: Verify Partition Table

```bash
# Check your partition layout
esptool.py --chip esp32s3 read_flash_status

# Or via Arduino Serial Monitor:
void diagnostics() {
  String label;
  uint32_t addr;
  OTAUpdater::getRunningPartitionInfo(label, addr);
  Serial.printf("Running: %s @ 0x%x\n", label.c_str(), addr);
}
```

### Test 3: Memory Check

```cpp
void test_memoryDuring OTA() {
  uint32_t before = esp_get_free_heap_size();
  OTAUpdater::performUpdate(url, size);
  uint32_t after = esp_get_free_heap_size();
  Serial.printf("Memory before: %u, after: %u\n", before, after);
}
```

---

## Next Steps

1. **Copy files** to your project (OTAUpdater.h/cpp)
2. **Add to platformio.ini** if needed
3. **Create test release** on GitHub
4. **Test checkForUpdate()** - verify it finds your release
5. **Test performUpdate()** - flash firmware on device
6. **Integrate with web server** - add `/api/ota/` endpoints
7. **Add scheduled checks** - create OTA task for daily checks
8. **Deploy** - merge changes and push to main

---

## Reference Links

- ESP-IDF OTA: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html
- Arduino Update.h: https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/Update.h
- GitHub REST API: https://docs.github.com/en/rest/releases/releases
- ArduinoJson: https://arduinojson.org/

---

**Need help?** Check the full documentation in `docs/OTA_FIRMWARE_UPDATES.md`
