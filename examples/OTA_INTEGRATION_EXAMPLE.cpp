//
// OTA Integration Example
// Shows how to integrate OTAUpdater into your project
//
//

#include <Arduino.h>
#include "OTAUpdater.h"

// ============================================================================
// Example 1: Simple OTA Check and Update
// ============================================================================

/**
 * Check GitHub for firmware updates and apply if available
 * Call this periodically (e.g., once per day)
 */
void example_simpleOTACheck() {
  // Configuration
  const char* GITHUB_OWNER = "your-username";     // Update this
  const char* GITHUB_REPO = "untitled";           // Your repo name

  // Check for update
  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate(GITHUB_OWNER, GITHUB_REPO, info)) {
    Serial.println("Failed to check for updates or no release found");
    return;
  }

  Serial.printf("Update available: %s\n", info.version.c_str());
  Serial.printf("Size: %zu bytes\n", info.size);

  // Check memory before updating
  if (!OTAUpdater::hasEnoughMemory()) {
    Serial.println("Insufficient memory for OTA");
    return;
  }

  // Perform update
  if (OTAUpdater::performUpdate(info.downloadUrl, info.size)) {
    Serial.println("OTA successful - rebooting in 5 seconds");
    delay(5000);
    ESP.restart();
  } else {
    Serial.println("OTA failed");
  }
}

// ============================================================================
// Example 2: OTA with Progress Callback
// ============================================================================

/**
 * Download firmware with progress feedback
 * Useful for updating UI elements during download
 */
void example_OTAWithProgress() {
  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate("your-username", "untitled", info)) {
    return;
  }

  // Progress callback lambda
  auto progressCallback = [](int percent, size_t bytesDownloaded) {
    Serial.printf("Download: %d%% (%zu bytes)\n", percent, bytesDownloaded);

    // Could update LED or web UI here
    // showProgress(percent);
  };

  if (OTAUpdater::performUpdate(info.downloadUrl, info.size, progressCallback)) {
    OTAUpdater::confirmBoot();
    ESP.restart();
  }
}

// ============================================================================
// Example 3: Safe OTA with Boot Verification
// ============================================================================

/**
 * Perform OTA with confirmation callback
 * Device must call confirmBoot() within timeout or will rollback
 */

bool otaBootVerificationPending = false;
unsigned long otaBootVerificationStart = 0;
const unsigned long BOOT_VERIFICATION_TIMEOUT = 60000;  // 60 seconds

void example_safeOTAWithVerification() {
  FirmwareInfo info;
  if (!OTAUpdater::checkForUpdate("your-username", "untitled", info)) {
    return;
  }

  // Check if running unconfirmed firmware
  if (OTAUpdater::hasUnconfirmedUpdate()) {
    Serial.println("Previous OTA not yet confirmed - confirming now");
    OTAUpdater::confirmBoot();
    return;
  }

  // Perform OTA
  if (OTAUpdater::performUpdate(info.downloadUrl, info.size)) {
    Serial.println("OTA successful - rebooting");
    delay(2000);
    ESP.restart();
  }
}

/**
 * Call this in your main loop after boot
 * Verifies new firmware is working, confirms OTA if OK
 */
void loop_handleBootVerification() {
  if (otaBootVerificationPending) {
    // Check if verification timeout exceeded
    if (millis() - otaBootVerificationStart > BOOT_VERIFICATION_TIMEOUT) {
      Serial.println("Boot verification timeout - update will auto-rollback on next boot");
      otaBootVerificationPending = false;
      return;
    }

    // Add your firmware verification logic here
    // Example: Check if LEDs are working, network is connected, etc.
    if (firmwareVerification_AllChecksPass()) {
      Serial.println("Firmware verified successfully");
      OTAUpdater::confirmBoot();
      otaBootVerificationPending = false;
    }
  }
}

bool firmwareVerification_AllChecksPass() {
  // TODO: Add your verification logic
  // - Check LED functionality
  // - Verify network connection
  // - Test critical features
  return true;
}

// ============================================================================
// Example 4: Scheduled OTA Task
// ============================================================================

/**
 * FreeRTOS task that periodically checks for updates
 * Creates a task in setup():
 *   xTaskCreatePinnedToCore(otaTaskFunction, "OTA", 4096, nullptr, 1, nullptr, 0);
 */
[[noreturn]] void otaTaskFunction(void* pvParameters) {
  const TickType_t UPDATE_CHECK_INTERVAL = (24 * 60 * 60 * 1000) / portTICK_PERIOD_MS;  // 24 hours

  while (true) {
    // Only check if connected to WiFi
    // if (WiFi.status() == WL_CONNECTED) {
    //   example_simpleOTACheck();
    // }

    vTaskDelay(UPDATE_CHECK_INTERVAL);
  }
}

// ============================================================================
// Example 5: Web API Endpoints (AsyncWebServer)
// ============================================================================

/**
 * Add to your WebServerManager class
 */

void webserver_setupOTAEndpoints() {
  // Example with AsyncWebServer (your project uses this)

  // GET /api/ota/check
  // Returns: { "version": "v1.0.0", "size": 1024000, "downloadUrl": "..." }
  /*
  server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request) {
    FirmwareInfo info;
    if (!OTAUpdater::checkForUpdate("your-username", "untitled", info)) {
      request->send(503, "application/json", R"({"error":"Failed to check GitHub"})");
      return;
    }

    DynamicJsonDocument doc(512);
    doc["version"] = info.version;
    doc["size"] = (uint32_t)info.size;
    doc["downloadUrl"] = info.downloadUrl;
    doc["notes"] = info.releaseNotes;

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // POST /api/ota/update
  // Body: { "downloadUrl": "...", "size": 1024000 }
  server.on("/api/ota/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if update already in progress
    static bool updateInProgress = false;
    if (updateInProgress) {
      request->send(409, "text/plain", "Update already in progress");
      return;
    }

    // Validate parameters
    if (!request->hasParam("downloadUrl") || !request->hasParam("size")) {
      request->send(400, "text/plain", "Missing downloadUrl or size");
      return;
    }

    String downloadUrl = request->getParam("downloadUrl")->value();
    size_t size = strtoul(request->getParam("size")->value().c_str(), nullptr, 10);

    // Send accepted response
    request->send(202, "text/plain", "OTA update started");

    // Perform update asynchronously
    updateInProgress = true;
    if (OTAUpdater::performUpdate(downloadUrl, size)) {
      delay(2000);
      ESP.restart();
    } else {
      Serial.println("OTA failed");
      updateInProgress = false;
    }
  });
  */
}

// ============================================================================
// Example 6: Diagnostic Commands
// ============================================================================

void printOTADiagnostics() {
  Serial.println("\n=== OTA Diagnostics ===");

  // Running partition
  String label;
  uint32_t address;
  if (OTAUpdater::getRunningPartitionInfo(label, address)) {
    Serial.printf("Running partition: %s (0x%x)\n", label.c_str(), address);
  }

  // Memory status
  uint32_t freeHeap, minFree, psramFree;
  OTAUpdater::getMemoryInfo(freeHeap, minFree, psramFree);
  Serial.printf("Heap: %u bytes free (min: %u)\n", freeHeap, minFree);
  Serial.printf("PSRAM: %u bytes free\n", psramFree);

  // Unconfirmed update check
  if (OTAUpdater::hasUnconfirmedUpdate()) {
    Serial.println("WARNING: Device running unconfirmed OTA update!");
    Serial.println("Call OTAUpdater::confirmBoot() to finalize");
  }

  Serial.println("========================\n");
}

// ============================================================================
// Example 7: GPIO-Triggered OTA Mode
// ============================================================================

#define OTA_FORCE_PIN 8  // Adjust to your hardware

void checkOTAForcePin() {
  gpio_set_direction((gpio_num_t)OTA_FORCE_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en((gpio_num_t)OTA_FORCE_PIN);

  delay(100);

  if (gpio_get_level((gpio_num_t)OTA_FORCE_PIN) == 0) {
    Serial.println("OTA force pin detected - entering OTA mode");
    // Disable normal tasks, enable web-only mode
    // vTaskSuspend(ledShowTaskHandle);
    // Stay in setup loop waiting for OTA
  }
}

// ============================================================================
// Example Usage in main.cpp
// ============================================================================

/*
#include "OTAUpdater.h"

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ... your normal setup ...

  // Check for unconfirmed OTA from previous boot
  if (OTAUpdater::hasUnconfirmedUpdate()) {
    Serial.println("Confirming previous OTA update...");
    OTAUpdater::confirmBoot();
  }

  // Print diagnostics
  printOTADiagnostics();

  // Start OTA checking task (optional)
  // xTaskCreatePinnedToCore(otaTaskFunction, "OTA", 4096, nullptr, 1, nullptr, 0);
}

void loop() {
  // Your normal loop code...

  // Periodically check for updates (example: every 24 hours)
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 24 * 60 * 60 * 1000) {
    example_simpleOTACheck();
    lastCheck = millis();
  }
}
*/
