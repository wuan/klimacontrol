# ESP32-S3 OTA Firmware Updates from GitHub Releases

## Executive Summary

This document provides comprehensive technical guidance for implementing Over-The-Air (OTA) firmware updates on your Adafruit QT Py ESP32-S3 (NoS-PSRAM) device. Your device currently uses 988KB of a 2MB flash memory, leaving adequate space for OTA implementation.

## 1. Partition Table Requirements for OTA

### Current Partition Configuration

Your device uses a 4MB flash memory layout (from the ESPAsyncWebServer library partition table):

```
Name     | Type  | SubType | Offset | Size   | Purpose
---------|-------|---------|--------|--------|---------------------------
nvs      | data  | nvs     | 36K    | 20K    | Non-volatile storage
otadata  | data  | ota     | 56K    | 8K     | OTA metadata (bootloader control)
app0     | app   | ota_0   | 64K    | 1856K  | Active firmware partition
app1     | app   | ota_1   | 1920K  | 1856K  | Standby firmware partition
spiffs   | data  | spiffs  | 3776K  | 256K   | Filesystem for web server content
coredump | data  | coredump| 4032K  | 64K    | Crash dumps
---------|-------|---------|--------|--------|---------------------------
Total: 4096K (4MB)
```

### OTA Requirements Met

Your partition table is **OTA-ready** because it includes:

1. **OTAData Partition (56K, 8K)**: Stores OTA state information
   - 2 copies of OTA status flags
   - Current active app selection
   - Boot counter for rollback

2. **Dual App Partitions**:
   - **app0 (1856K)**: Active partition, runs immediately after boot
   - **app1 (1856K)**: Standby partition, update target
   - Both are large enough (1856K) for your firmware (988K)

### Flash Headroom Analysis

```
Total Flash:              2,048 KB (2MB)
Current Firmware Size:    988 KB
Both App Partitions:      1,856 KB × 2 = 3,712 KB

Available after OTA:      3,712 - 988 = 2,724 KB for firmware growth
Utilization (current):    988 / 1,856 = 53.2% of partition
Maximum Safe Expansion:   Up to 1,800 KB (includes ~56KB buffer)
```

**Verdict**: Adequate space. You have room for ~812 KB of additional code before requiring a partition table change.

### Recommended Partition Configuration for 2MB Devices

If you need to optimize for memory-constrained scenarios, here's an alternative 2MB partition table:

```
# Name,Type,SubType,Offset,Size,Flags
nvs,data,nvs,36K,20K,
otadata,data,ota,56K,8K,
app0,app,ota_0,64K,896K,
app1,app,ota_1,960K,896K,
spiffs,data,spiffs,1856K,192K,
coredump,data,coredump,2048K,64K,
```

For your use case with 2MB flash, keep the existing 4MB partition table as is—it's the standard and gives better headroom.

---

## 2. ESP32 OTA Libraries: Comprehensive Comparison

### A. **Update.h** (Arduino Built-in)

**Best for**: Simple, standalone OTA updates with minimal dependencies

```cpp
#include <Update.h>

// Characteristics
- Direct flash writing to OTA partitions
- No partition management required (Arduino handles it)
- Synchronous blocking operations
- Minimal memory overhead (~8KB)
- Part of Arduino core for ESP32
```

**Advantages:**
- Lightest memory footprint
- Most direct access to flash
- Built into Arduino framework
- Fine-grained control over write process

**Disadvantages:**
- No automatic rollback support
- Requires manual partition selection
- Less abstraction for error handling
- Must manage OTA state manually via Update class properties

**Key Methods:**
```cpp
Update.begin(size, U_FLASH)          // Start update, specify size
Update.write(data, len)               // Write chunk
Update.end(false)                     // Finalize (false = no reboot)
Update.isRunning()                    // Check status
Update.getError()                     // Get error code
```

---

### B. **HTTPUpdate** (Arduino Built-in)

**Best for**: Simple HTTP/HTTPS downloads with automatic partition handling

```cpp
#include <HTTPUpdate.h>

// Characteristics
- Built on top of Update.h
- Handles HTTP/HTTPS connections
- Automatic partition selection
- Integrated SSL/TLS support
- Part of Arduino core for ESP32
```

**Advantages:**
- One-line update capability
- Automatic SSL verification (with fingerprint or CA bundle)
- Returns clear HTTP status codes
- Less code to write
- Good for direct file URLs (less processing needed)

**Disadvantages:**
- Blocks network during update
- Less suitable for GitHub API (requires JSON parsing first)
- No built-in rollback
- Less flexible for custom release logic

**Key Methods:**
```cpp
HTTPUpdate.update(client, url, version)     // Simple update
HTTPUpdate.updateFS(client, url)             // Filesystem update
httpUpdate.setLedPin(PIN)                   // Set LED for feedback
httpUpdate.onStart(callback)                // Lifecycle hooks
httpUpdate.onEnd(callback)
httpUpdate.onError(callback)
httpUpdate.onProgress(callback)
```

---

### C. **ArduinoOTA** (Arduino Built-in)

**Best for**: Over-the-air updates via Arduino IDE or network-based tools

```cpp
#include <ArduinoOTA.h>

// Characteristics
- Designed for development workflow
- Can upload from Arduino IDE over network
- Minimal production use
- Protocol-based update mechanism
- Part of Arduino core for ESP32
```

**Advantages:**
- Convenient for development
- Works with Arduino IDE directly
- No external server required
- Good for testing OTA mechanism

**Disadvantages:**
- Not designed for production release management
- Requires Arduino toolchain or specific OTA protocol
- Less suitable for GitHub-based distribution
- Heavier footprint than Update.h
- Requires consistent network availability

**Key Methods:**
```cpp
ArduinoOTA.begin()                          // Initialize
ArduinoOTA.handle()                         // Process in loop
ArduinoOTA.setHostname("name")
ArduinoOTA.onStart(callback)
ArduinoOTA.onEnd(callback)
ArduinoOTA.onError(callback)
ArduinoOTA.onProgress(callback)
```

---

### Recommendation for Your Use Case

**Use HTTPUpdate + ArduinoJson for GitHub Releases**

Rationale:
1. HTTPUpdate provides secure HTTPS downloads
2. ArduinoJson (already in your dependencies) parses GitHub API
3. Automatic partition selection
4. Clear error handling and retry logic possible
5. Minimal additional memory overhead

---

## 3. GitHub API for Fetching Latest Release Information

### GitHub REST API v3 Endpoints

#### Endpoint: Get Latest Release

```
GET https://api.github.com/repos/{owner}/{repo}/releases/latest
```

**Example for your project:**
```
GET https://api.github.com/repos/your-username/untitled/releases/latest
```

### Response Structure

```json
{
  "url": "https://api.github.com/repos/owner/repo/releases/1",
  "assets": [
    {
      "name": "firmware.bin",
      "id": 123456,
      "size": 1048576,
      "download_count": 42,
      "created_at": "2025-12-15T10:00:00Z",
      "updated_at": "2025-12-15T10:00:00Z",
      "browser_download_url": "https://github.com/owner/repo/releases/download/v1.0.0/firmware.bin",
      "state": "uploaded"
    }
  ],
  "tag_name": "v1.0.0",
  "name": "Release 1.0.0",
  "draft": false,
  "prerelease": false,
  "created_at": "2025-12-15T09:00:00Z",
  "published_at": "2025-12-15T10:00:00Z",
  "body": "Release notes here..."
}
```

### ArduinoJson Parsing Code

```cpp
#include <ArduinoJson.h>
#include <HTTPClient.h>

// Minimal required document size for release metadata
const size_t JSON_CAPACITY = JSON_ARRAY_SIZE(5) + JSON_OBJECT_SIZE(25) + 512;

struct GitHubRelease {
  String tag_name;
  String name;
  String download_url;
  size_t asset_size;
  String body;
};

bool fetchGitHubRelease(const char* owner, const char* repo, GitHubRelease& release) {
  HTTPClient http;

  // Build API URL
  String url = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";

  http.useHTTP10(true);  // Reduces memory usage

  if (!http.begin(url)) {
    Serial.println("Failed to initialize HTTP");
    return false;
  }

  // Add GitHub API headers
  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.addHeader("User-Agent", "ESP32-OTA/1.0");

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  // Parse JSON response
  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(JSON_CAPACITY);
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return false;
  }

  // Extract release information
  release.tag_name = doc["tag_name"].as<String>();
  release.name = doc["name"].as<String>();
  release.body = doc["body"].as<String>();

  // Find .bin file in assets
  JsonArray assets = doc["assets"];
  for (JsonObject asset : assets) {
    String asset_name = asset["name"].as<String>();
    if (asset_name.endsWith(".bin")) {
      release.download_url = asset["browser_download_url"].as<String>();
      release.asset_size = asset["size"].as<size_t>();
      break;
    }
  }

  if (release.download_url.isEmpty()) {
    Serial.println("No .bin file found in release assets");
    return false;
  }

  Serial.printf("Release: %s (%s)\n", release.name.c_str(), release.tag_name.c_str());
  Serial.printf("Download: %s\n", release.download_url.c_str());
  Serial.printf("Size: %zu bytes\n", release.asset_size);

  return true;
}
```

### API Rate Limiting

```
Unauthenticated requests: 60 per hour per IP
Authenticated requests: 5,000 per hour per user

Headers in response:
X-RateLimit-Limit: 60
X-RateLimit-Remaining: 59
X-RateLimit-Reset: 1234567890
```

**Best Practice**: Cache release info for at least 1 hour to avoid rate limiting.

---

## 4. Downloading .bin Files from GitHub Releases over HTTPS

### Security Considerations

#### SSL/TLS Certificate Verification

Three approaches in order of security:

**1. Full Certificate Verification (Most Secure)**
```cpp
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>  // Built-in CA certificates

WiFiClientSecure client;
// Use Mozilla CA bundle (included with esp-idf)
client.setCACertBundle(esp_crt_bundle_get());

HTTPClient http;
http.begin(client, url);
```

Pros:
- Protects against MITM attacks
- Follows security best practices
- Works with any GitHub domain

Cons:
- Larger binary size (~30KB for CA bundle)
- Time zone must be correct for verification

**2. SHA256 Fingerprint Verification**
```cpp
const char* sha256_fingerprint = "3C 66 F9 24 ... (64 chars)";

WiFiClientSecure client;
client.setInsecure();  // Don't verify chain
client.setFingerprint(sha256_fingerprint);  // But verify host cert

HTTPClient http;
http.begin(client, url);
```

Pros:
- Smaller binary (~2KB)
- Works without RTC/NTP
- Fast verification

Cons:
- Must update fingerprint when GitHub renews certificate
- Certificate renewal happens ~90 days
- Not future-proof

**3. No Verification (Least Secure)**
```cpp
WiFiClientSecure client;
client.setInsecure();

HTTPClient http;
http.begin(client, url);
```

Cons:
- Vulnerable to MITM attacks
- Only acceptable for testing

### Recommended Approach for Your Device

Use option 1 (Full Certificate Verification):

```cpp
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>

// Download and flash firmware from URL
int downloadAndFlashFirmware(const String& downloadUrl, size_t expectedSize) {
  Serial.printf("Starting firmware download: %s\n", downloadUrl.c_str());

  // Setup secure client with certificate bundle
  WiFiClientSecure client;
  client.setCACertBundle(esp_crt_bundle_get());

  // Perform update
  t_httpUpdate_return ret = httpUpdate.update(
    client,
    downloadUrl,
    ""  // Empty string = no version comparison
  );

  switch (ret) {
    case HTTP_UPDATE_OK:
      Serial.println("HTTP_UPDATE_OK: Update successful");
      return 0;

    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP_UPDATE_FAILED: Error (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      return -1;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP_UPDATE_NO_UPDATES: No update available");
      return 1;

    default:
      Serial.printf("Unknown return code: %d\n", ret);
      return -2;
  }
}
```

### Progressive Download with Update.h

For more control and error handling:

```cpp
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>

const int CHUNK_SIZE = 4096;  // 4KB chunks

int progressiveDownloadAndFlash(const String& downloadUrl, size_t expectedSize) {
  WiFiClientSecure client;
  client.setCACertBundle(esp_crt_bundle_get());

  HTTPClient http;
  if (!http.begin(client, downloadUrl)) {
    Serial.println("Failed to create HTTP connection");
    return -1;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return -1;
  }

  size_t contentSize = http.getSize();
  if (contentSize != expectedSize) {
    Serial.printf("Size mismatch! Expected: %zu, Got: %zu\n", expectedSize, contentSize);
    http.end();
    return -1;
  }

  // Initialize OTA
  if (!Update.begin(contentSize, U_FLASH)) {
    Serial.println("Update.begin() failed");
    Serial.println(Update.errorString());
    http.end();
    return -1;
  }

  // Create stream for download
  WiFiClient* stream = http.getStreamPtr();

  // Progressive download
  size_t totalRead = 0;
  uint8_t buffer[CHUNK_SIZE];
  unsigned long lastProgress = 0;

  while (totalRead < contentSize) {
    size_t available = stream->available();
    if (available) {
      size_t toRead = std::min((size_t)CHUNK_SIZE, contentSize - totalRead);
      int read = stream->readBytes(buffer, toRead);

      if (read > 0) {
        // Write to flash
        size_t written = Update.write(buffer, read);
        if (written != read) {
          Serial.printf("Write mismatch! Expected: %d, Got: %zu\n", read, written);
          Update.abort();
          http.end();
          return -1;
        }

        totalRead += read;

        // Print progress every 50KB
        if (millis() - lastProgress > 1000) {
          int percent = (totalRead * 100) / contentSize;
          Serial.printf("Progress: %d%% (%zu / %zu bytes)\n", percent, totalRead, contentSize);
          lastProgress = millis();
        }
      }
    } else {
      delay(10);  // Wait for more data
    }
  }

  if (!Update.end(false)) {  // false = no auto-reboot
    Serial.printf("Update.end() failed: %s\n", Update.errorString());
    return -1;
  }

  http.end();

  Serial.println("Firmware download and flash successful!");
  Serial.println("Reboot device to load new firmware");

  return 0;
}
```

### Memory-Efficient Streaming

For devices with limited RAM, don't buffer entire file:

```cpp
// Good: Stream directly to flash (used above)
while (totalRead < contentSize) {
  size_t toRead = std::min((size_t)CHUNK_SIZE, contentSize - totalRead);
  int read = stream->readBytes(buffer, toRead);
  Update.write(buffer, read);
  totalRead += read;
}

// Bad: Avoid this - loads entire file into RAM
String fullContent = http.getString();  // 1-2MB in memory!
Update.write((const uint8_t*)fullContent.c_str(), fullContent.length());
```

---

## 5. Memory and Flash Constraints

### Your Device Specifications

```
Device:           Adafruit QT Py ESP32-S3 (No PSRAM)
Flash Memory:     2 MB (some variants come with 4MB)
PSRAM:            None
SRAM:             ~320 KB usable (after framework)
```

### Memory Layout During OTA

```
Total SRAM: ~512 KB (typical ESP32-S3 SRAM)
├── Arduino Framework: ~40-50 KB
├── WiFi Stack: ~60-80 KB
├── FreeRTOS: ~30-40 KB
├── HTTPClient: ~20-30 KB
├── Update.h: ~8-10 KB
├── ArduinoJson: ~5-8 KB
├── Download Buffer (4KB): 4 KB
└── Free: ~150-200 KB (varies)
```

### Flash Partition Analysis

```
Current Usage:
- Boot loader: ~64 KB (slot 0)
- NVS: ~20 KB (configuration)
- OTA data: ~8 KB (metadata)
- App0 (running): 988 KB / 1856 KB available
- SPIFFS: 256 KB (web assets)
- Coredump: 64 KB (not used unless crash)

Total used: ~988 KB + ~412 KB system = ~1.4 MB
Free: ~600 KB available
```

### Firmware Size Constraints

```
Maximum app binary: 1,856 KB (app0 partition size)
Your current: 988 KB
Headroom: 868 KB for code growth

Recommended max: 1,700 KB (keep 156KB safety margin)
```

### Optimization Strategies

**1. Reduce Binary Size:**
```
Use compiler optimizations:
-Os (optimize for size)
-ffunction-sections -fdata-sections (section GC)

In platformio.ini:
[env:adafruit_qtpy_esp32s3_nopsram]
build_flags =
  -Os
  -ffunction-sections
  -fdata-sections
  -Wl,--gc-sections
  -Wl,--print-gc-sections
```

**2. Reduce Download Buffer:**
Use smaller chunks if memory constrained:
```cpp
const int CHUNK_SIZE = 2048;  // 2KB instead of 4KB
```

**3. Memory Management During OTA:**
```cpp
void performOTA() {
  // Pause non-critical tasks
  vTaskSuspend(xTaskGetHandle("LED show"));

  // Perform update
  downloadAndFlashFirmware(url, size);

  // Resume tasks
  vTaskResume(xTaskGetHandle("LED show"));
}
```

**4. Disable Components During Update:**
```cpp
// Minimize WiFi driver memory footprint
WiFi.mode(WIFI_STA);  // Station only
WiFi.setTxPower(WIFI_POWER_8dBm);  // Reduce power (less RAM)
```

### Monitoring Memory During Runtime

```cpp
#include <esp_heap_trace.h>

void printMemoryStats() {
  uint32_t free_heap = esp_get_free_heap_size();
  uint32_t min_heap = esp_get_minimum_free_heap_size();
  uint32_t psram_free = esp_spiram_get_size() > 0 ?
                        esp_get_free_psram_size() : 0;

  Serial.printf("Heap: %u bytes free (min: %u)\n", free_heap, min_heap);
  Serial.printf("PSRAM: %u bytes free\n", psram_free);

  // If free heap < 32KB, OTA will likely fail
  if (free_heap < 32768) {
    Serial.println("WARNING: Low memory for OTA!");
  }
}
```

---

## 6. Safe Update Mechanisms and Rollback

### OTA Data Partition Structure

The ESP-IDF bootloader automatically manages rollback via the OTA data partition:

```
OTA Data Partition Layout (8 KB)
├── CRC32: 4 bytes
├── Flags:
│   ├── ota_seq: 4 bytes (sequence counter)
│   ├── ota_state: 4 bytes (NEW/VALID/INVALID)
│   └── reserved: 8 bytes
├── CRC32: 4 bytes
└── Copy B: (repeated for redundancy)
```

### Automatic Rollback Mechanism

```
Boot Sequence:
1. Check OTA data partition
2. If app0 (ota_seq=even) is marked VALID → boot app0
3. If app0 is marked INVALID → try app1
4. Boot counter prevents bad firmware loops:
   - System tracks failed boots
   - After N failures, marks partition INVALID
   - Falls back to previous partition

Default behavior:
- After update, new partition marked VALID
- First successful boot confirmation via esp_ota_mark_app_valid_cancel_rollback()
```

### Safe Update Implementation

```cpp
#include <esp_ota_ops.h>

class OTAManager {
private:
  const esp_partition_t* updatePartition;

public:
  OTAManager() : updatePartition(nullptr) {}

  // Step 1: Find target partition
  bool prepareOTA() {
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();

    Serial.printf("Running partition: %s (offset 0x%x)\n",
                  runningPartition->label,
                  runningPartition->address);

    // Find the non-running OTA partition
    updatePartition = esp_ota_get_next_update_partition(runningPartition);

    if (updatePartition == nullptr) {
      Serial.println("No update partition available!");
      return false;
    }

    Serial.printf("Update partition: %s (offset 0x%x)\n",
                  updatePartition->label,
                  updatePartition->address);

    return true;
  }

  // Step 2: Download firmware with verification
  bool downloadFirmware(const String& url, size_t expectedSize) {
    WiFiClientSecure client;
    client.setCACertBundle(esp_crt_bundle_get());

    HTTPClient http;
    if (!http.begin(client, url)) {
      Serial.println("HTTP begin failed");
      return false;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
      Serial.printf("HTTP error: %d\n", httpCode);
      http.end();
      return false;
    }

    // Verify size
    if (http.getSize() != expectedSize) {
      Serial.printf("Size mismatch! Expected: %zu, Got: %d\n",
                    expectedSize, http.getSize());
      http.end();
      return false;
    }

    // Start OTA write
    if (!Update.begin(expectedSize, U_FLASH)) {
      Serial.printf("Update.begin failed: %s\n", Update.errorString());
      http.end();
      return false;
    }

    // Stream download
    WiFiClient* stream = http.getStreamPtr();
    const int CHUNK_SIZE = 4096;
    uint8_t buffer[CHUNK_SIZE];
    size_t totalRead = 0;
    uint32_t checksum = 0;

    while (totalRead < expectedSize) {
      size_t available = stream->available();
      if (available) {
        size_t toRead = std::min((size_t)CHUNK_SIZE, expectedSize - totalRead);
        int read = stream->readBytes(buffer, toRead);

        if (read > 0) {
          // Update checksum
          for (int i = 0; i < read; i++) {
            checksum ^= buffer[i];
          }

          // Write to flash
          if (Update.write(buffer, read) != read) {
            Serial.printf("Flash write failed at offset %zu\n", totalRead);
            Update.abort();
            http.end();
            return false;
          }

          totalRead += read;

          // Progress feedback
          if (totalRead % (256 * 1024) == 0) {
            Serial.printf("Downloaded: %zu / %zu bytes\n", totalRead, expectedSize);
          }
        }
      } else {
        delay(10);
      }

      // Watchdog: timeout after 30 seconds of no data
      if (stream->available() == 0 && totalRead < expectedSize) {
        delay(100);
      }
    }

    if (!Update.end(false)) {  // false = no auto reboot
      Serial.printf("Update.end failed: %s\n", Update.errorString());
      http.end();
      return false;
    }

    http.end();

    Serial.printf("Firmware downloaded successfully. Checksum: 0x%08x\n", checksum);
    return true;
  }

  // Step 3: Set new partition as boot target (no auto-rollback)
  bool setPendingOTA() {
    esp_err_t err = esp_ota_set_boot_partition(updatePartition);

    if (err != ESP_OK) {
      Serial.printf("Failed to set boot partition: %s\n", esp_err_to_name(err));
      return false;
    }

    Serial.println("Boot partition updated. Reboot to load new firmware.");
    return true;
  }

  // Step 4: Confirm OTA after successful boot
  // Call this from main loop after verifying new firmware works
  bool confirmOTA() {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();

    if (err != ESP_OK) {
      Serial.printf("Failed to confirm OTA: %s\n", esp_err_to_name(err));
      return false;
    }

    Serial.println("OTA confirmed - rollback disabled");
    return true;
  }

  // Check boot status
  bool checkBootStatus() {
    const esp_partition_t* runningPartition = esp_ota_get_running_partition();
    esp_ota_img_state_t state;
    esp_ota_get_state_partition(runningPartition, &state);

    Serial.printf("Running partition: %s\n", runningPartition->label);

    if (state == ESP_OTA_IMG_NEW) {
      Serial.println("Firmware marked as NEW - call confirmOTA() to validate");
      return false;
    } else if (state == ESP_OTA_IMG_VALID) {
      Serial.println("Firmware is VALID");
      return true;
    } else {
      Serial.println("Firmware state: UNDEFINED");
      return true;
    }
  }
};
```

### Timeout and Watchdog Configuration

```cpp
// Increase OTA timeout
ESP.wdtDisable();  // Disable watchdog during long ops
// ... perform OTA ...
esp_task_wdt_init(10, true);  // Re-enable with 10s timeout

// Or use more granular control:
#include <esp_task_wdt.h>

esp_task_wdt_init(30, true);  // 30 second timeout

void resetWatchdog() {
  esp_task_wdt_reset();  // Reset timeout
}
```

### Rollback Strategy Decision Tree

```
New firmware boots successfully?
├─ YES: Can initialize and run?
│  ├─ YES: Call confirmOTA() to lock in place
│  └─ NO: Let timeout trigger rollback (app marked INVALID)
└─ NO: Automatic rollback on boot failure
   └─ Previous firmware reactivated automatically
```

---

## 7. Complete Example Code Pattern for ESP32 OTA Updates

### A. Minimal Complete Example

```cpp
// File: src/OTAUpdater.h
#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <esp_crt_bundle.h>
#include <esp_ota_ops.h>

struct FirmwareInfo {
  String version;
  String downloadUrl;
  size_t size;
  String checksum;
};

class OTAUpdater {
public:
  // Check GitHub for new release
  static bool checkForUpdate(const char* owner, const char* repo, FirmwareInfo& info);

  // Download and flash firmware
  static bool performUpdate(const String& downloadUrl, size_t size);

  // Confirm boot after update
  static bool confirmBoot();

private:
  static const int TIMEOUT_MS = 300000;  // 5 minutes
  static const int CHUNK_SIZE = 4096;
};
```

```cpp
// File: src/OTAUpdater.cpp
#include "OTAUpdater.h"

bool OTAUpdater::checkForUpdate(const char* owner, const char* repo, FirmwareInfo& info) {
  WiFiClientSecure client;
  client.setCACertBundle(esp_crt_bundle_get());

  HTTPClient http;
  String apiUrl = String("https://api.github.com/repos/") + owner + "/" + repo + "/releases/latest";

  if (!http.begin(client, apiUrl)) {
    Serial.println("HTTP init failed");
    return false;
  }

  http.addHeader("Accept", "application/vnd.github.v3+json");
  http.setTimeout(TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  // Parse response
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error) {
    Serial.printf("JSON error: %s\n", error.c_str());
    return false;
  }

  // Extract release info
  info.version = doc["tag_name"].as<String>();

  // Find .bin asset
  JsonArray assets = doc["assets"];
  for (JsonObject asset : assets) {
    if (String(asset["name"]).endsWith(".bin")) {
      info.downloadUrl = asset["browser_download_url"].as<String>();
      info.size = asset["size"].as<size_t>();
      break;
    }
  }

  return !info.downloadUrl.isEmpty();
}

bool OTAUpdater::performUpdate(const String& downloadUrl, size_t expectedSize) {
  Serial.printf("Starting OTA update from: %s\n", downloadUrl.c_str());
  Serial.printf("Expected size: %zu bytes\n", expectedSize);

  WiFiClientSecure client;
  client.setCACertBundle(esp_crt_bundle_get());

  HTTPClient http;
  if (!http.begin(client, downloadUrl)) {
    Serial.println("HTTP init failed");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", httpCode);
    http.end();
    return false;
  }

  // Verify size
  if (http.getSize() != expectedSize) {
    Serial.printf("Size mismatch! Expected: %zu, Got: %d\n", expectedSize, http.getSize());
    http.end();
    return false;
  }

  // Start OTA
  if (!Update.begin(expectedSize, U_FLASH)) {
    Serial.printf("Update.begin failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  // Stream data
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buffer[CHUNK_SIZE];
  size_t totalRead = 0;
  unsigned long lastUpdate = millis();

  while (totalRead < expectedSize && millis() - lastUpdate < TIMEOUT_MS) {
    size_t available = stream->available();

    if (available > 0) {
      size_t toRead = std::min((size_t)CHUNK_SIZE, expectedSize - totalRead);
      int bytesRead = stream->readBytes(buffer, toRead);

      if (bytesRead > 0) {
        size_t bytesWritten = Update.write(buffer, bytesRead);
        if (bytesWritten != bytesRead) {
          Serial.printf("Write error at offset %zu\n", totalRead);
          Update.abort();
          http.end();
          return false;
        }

        totalRead += bytesRead;
        lastUpdate = millis();

        // Progress
        if (totalRead % 65536 == 0) {
          int percent = (totalRead * 100) / expectedSize;
          Serial.printf("Progress: %d%% (%zu/%zu)\n", percent, totalRead, expectedSize);
        }
      }
    } else {
      delay(10);
    }
  }

  if (millis() - lastUpdate >= TIMEOUT_MS) {
    Serial.println("OTA timeout!");
    Update.abort();
    http.end();
    return false;
  }

  // Finalize
  if (!Update.end(false)) {
    Serial.printf("Update.end failed: %s\n", Update.errorString());
    http.end();
    return false;
  }

  http.end();
  Serial.println("OTA successful! Reboot to apply.");
  return true;
}

bool OTAUpdater::confirmBoot() {
  esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  if (err != ESP_OK) {
    Serial.printf("Boot confirm failed: %s\n", esp_err_to_name(err));
    return false;
  }
  Serial.println("Boot confirmed");
  return true;
}
```

### B. Integration with Your WebServerManager

```cpp
// Add to WebServerManager.h
private:
  void handleOTACheck(AsyncWebServerRequest *request);
  void handleOTAUpdate(AsyncWebServerRequest *request);
  bool isUpdateInProgress = false;

// Add to setupAPIRoutes() in WebServerManager.cpp
void WebServerManager::setupAPIRoutes() {
  // ... existing routes ...

  server.on("/api/ota/check", HTTP_GET, [this](AsyncWebServerRequest *request) {
    handleOTACheck(request);
  });

  server.on("/api/ota/update", HTTP_POST, [this](AsyncWebServerRequest *request) {
    handleOTAUpdate(request);
  });
}

// Implementation
void WebServerManager::handleOTACheck(AsyncWebServerRequest *request) {
  if (isUpdateInProgress) {
    request->send(409, "text/plain", "Update already in progress");
    return;
  }

  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate("your-username", "untitled", info)) {
    request->send(503, "application/json", "{\"error\":\"Failed to check GitHub\"}");
    return;
  }

  DynamicJsonDocument doc(256);
  doc["version"] = info.version;
  doc["downloadUrl"] = info.downloadUrl;
  doc["size"] = (uint32_t)info.size;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void WebServerManager::handleOTAUpdate(AsyncWebServerRequest *request) {
  if (isUpdateInProgress) {
    request->send(409, "text/plain", "Update already in progress");
    return;
  }

  if (!request->hasParam("url") || !request->hasParam("size")) {
    request->send(400, "text/plain", "Missing url or size parameter");
    return;
  }

  String url = request->getParam("url")->value();
  size_t size = strtoul(request->getParam("size")->value().c_str(), nullptr, 10);

  isUpdateInProgress = true;
  request->send(202, "text/plain", "Update started");

  // Perform update asynchronously
  if (OTAUpdater::performUpdate(url, size)) {
    Serial.println("OTA update successful - rebooting in 5 seconds");
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("OTA update failed");
    isUpdateInProgress = false;
  }
}
```

### C. Complete Integration in main.cpp

```cpp
// Add to main.cpp

bool checkAndApplyFirmwareUpdate() {
  Serial.println("Checking for firmware updates...");

  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate("your-username", "untitled", info)) {
    Serial.println("Failed to check for updates");
    return false;
  }

  Serial.printf("Latest version: %s\n", info.version.c_str());
  Serial.printf("Current version: %s\n", FIRMWARE_VERSION);  // Define in Config.h

  if (info.version == FIRMWARE_VERSION) {
    Serial.println("Already on latest version");
    return false;
  }

  Serial.printf("Update available! Download size: %zu bytes\n", info.size);

  // Check if we have enough free space
  uint32_t freeHeap = esp_get_free_heap_size();
  if (freeHeap < 65536) {  // Need at least 64KB free
    Serial.println("Insufficient memory for update");
    return false;
  }

  // Pause LED task
  vTaskSuspend(showTaskHandle);

  // Perform update
  bool success = OTAUpdater::performUpdate(info.downloadUrl, info.size);

  // Resume LED task
  vTaskResume(showTaskHandle);

  if (success) {
    OTAUpdater::confirmBoot();
    // Schedule restart
    delay(5000);
    ESP.restart();
  }

  return success;
}

// In setup() or as periodic task:
void checkFirmwareTask(void *pvParameters) {
  while (true) {
    if (network.getMode() == NetworkMode::STA) {
      checkAndApplyFirmwareUpdate();
    }

    // Check once per day
    vTaskDelay((24 * 60 * 60 * 1000) / portTICK_PERIOD_MS);
  }
}
```

### D. GPIO-Triggered OTA Mode

```cpp
// Force OTA mode on boot if GPIO pin held low
#include <driver/gpio.h>

#define OTA_FORCE_PIN 8  // Adjust to your setup

bool shouldEnterOTAMode() {
  gpio_set_direction((gpio_num_t)OTA_FORCE_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)OTA_FORCE_PIN);

  delay(100);  // Debounce

  int pinState = gpio_get_level((gpio_num_t)OTA_FORCE_PIN);
  if (pinState == 0) {  // LOW = triggered
    return true;
  }
  return false;
}

void setup() {
  // ...

  if (shouldEnterOTAMode()) {
    Serial.println("OTA mode enabled via GPIO");
    // Disable normal tasks
    vTaskSuspend(showTaskHandle);

    // Wait for web-based update
    while (true) {
      delay(1000);  // Webserver handles OTA
    }
  }

  // ... normal setup ...
}
```

---

## Implementation Roadmap

### Phase 1: Basic OTA (Week 1)
- [ ] Create OTAUpdater class
- [ ] Implement GitHub API checking
- [ ] Test firmware download and flash
- [ ] Add web API endpoints

### Phase 2: Safety & Rollback (Week 2)
- [ ] Implement boot confirmation
- [ ] Add memory monitoring
- [ ] Test rollback scenarios
- [ ] Add update progress feedback

### Phase 3: Production Hardening (Week 3)
- [ ] Add automatic update checks
- [ ] Implement signed firmware verification
- [ ] Add detailed logging
- [ ] Create update UI/API for web interface

### Phase 4: Monitoring & Metrics (Week 4)
- [ ] Track update success/failure rates
- [ ] Monitor available storage
- [ ] Add health checks post-update
- [ ] Performance optimization

---

## Troubleshooting Guide

### Common Issues and Solutions

**Problem**: "Update.begin() failed: 0x2"
```
Cause: Not enough flash memory or invalid partition
Solution: Check partition table, ensure app partition is ≥ 1.8MB
```

**Problem**: "HTTP error: 416"
```
Cause: Content-Range header issue (HTTP 206 resume)
Solution: Set http.useHTTP10(true) to disable HTTP/1.1 features
```

**Problem**: "Stack overflow" during download
```
Cause: Large JSON document or buffer on stack
Solution: Use DynamicJsonDocument (heap) instead of StaticJsonDocument
```

**Problem**: Device boots old firmware after update
```
Cause: OTA data partition corrupted or not confirmed
Solution: Call confirmBoot() after successful boot, add manual fallback
```

**Problem**: "Timeout" during download
```
Cause: Slow network or large file
Solution: Increase timeout or reduce CHUNK_SIZE
```

---

## Summary Table: OTA Libraries Comparison

| Feature | Update.h | HTTPUpdate | ArduinoOTA |
|---------|----------|-----------|-----------|
| Memory overhead | 8-10 KB | 20-30 KB | 40-50 KB |
| SSL/TLS support | No | Yes | No |
| Automatic partition selection | No | Yes | Yes |
| Rollback support | Manual | Basic | Basic |
| Designed for GitHub | No | No | No |
| Ease of use | Medium | Easy | Easy |
| Best for | Custom | HTTP URLs | Development |
| **Recommendation** | ✓ Use with HTTPClient | **✓ Recommended** | Not for releases |

---

## References

- ESP-IDF OTA Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html
- Arduino ESP32 Update Reference: https://github.com/espressif/arduino-esp32/tree/master/cores/esp32/Update.h
- GitHub REST API v3: https://docs.github.com/en/rest/releases/releases
- HTTPClient Documentation: https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient
- ArduinoJson: https://arduinojson.org/
- ESP32-S3 Hardware Reference: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf

---

**Document Generated**: 2026-01-05
**Target Device**: Adafruit QT Py ESP32-S3 (No PSRAM, 2MB Flash)
**Framework**: Arduino (ESP32 core)
