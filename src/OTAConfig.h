//
// OTA Configuration Template
// Customize these settings for your project
//
//

#pragma once

// ============================================================================
// GitHub Configuration
// ============================================================================

// Your GitHub username/organization
#define OTA_GITHUB_OWNER "oetztal"

// Your repository name
#define OTA_GITHUB_REPO "ledz"

// ============================================================================
// Firmware Version
// ============================================================================

// Firmware version is injected at build time from git tag via scripts/get_version.py
// If not building with PlatformIO (e.g., manual compilation), fallback to this version
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v0.0.0-dev"
#endif

// Build timestamp (optional, for diagnostics)
#define FIRMWARE_BUILD_DATE __DATE__
#define FIRMWARE_BUILD_TIME __TIME__

// ============================================================================
// OTA Behavior Configuration
// ============================================================================

// Enable automatic update checks on startup
#define OTA_AUTO_CHECK_ON_BOOT 0

// Enable periodic update checks (requires OTA task)
#define OTA_AUTO_CHECK_PERIODIC 0

// Periodic check interval in hours
#define OTA_CHECK_INTERVAL_HOURS 24

// Enable GPIO pin to force OTA mode
#define OTA_ENABLE_FORCE_PIN 0

// GPIO pin for forcing OTA mode (held LOW during boot)
#define OTA_FORCE_PIN 8

// ============================================================================
// OTA Safety Configuration
// ============================================================================

// Auto-confirm firmware after N seconds of successful boot
// Set to 0 to disable auto-confirm (requires manual confirmBoot() call)
#define OTA_AUTO_CONFIRM_DELAY_MS 0

// Boot verification timeout (ms)
// If firmware doesn't confirm within this time, will rollback on next boot
#define OTA_BOOT_VERIFY_TIMEOUT_MS 300000  // 5 minutes

// Require minimum free heap before attempting OTA (bytes)
#define OTA_MIN_FREE_HEAP_BYTES 65536  // 64 KB

// ============================================================================
// Network Configuration
// ============================================================================

// HTTP request timeout (milliseconds)
#define OTA_HTTP_TIMEOUT_MS 300000  // 5 minutes

// Download chunk size (bytes)
// Smaller = less RAM usage, slower download
// Larger = faster download, more RAM needed
#define OTA_DOWNLOAD_CHUNK_SIZE 4096  // 4 KB

// Use HTTP/1.0 instead of HTTP/1.1 (reduces overhead)
#define OTA_USE_HTTP10 1

// ============================================================================
// LED/UI Feedback Configuration
// ============================================================================

// Enable LED feedback during OTA
#define OTA_ENABLE_LED_FEEDBACK 0

// LED pin for OTA feedback
#define OTA_LED_PIN 0

// LED feedback behavior during download
// 0 = constant on
// 1 = pulsing
// 2 = blinking progress
#define OTA_LED_FEEDBACK_MODE 1

// ============================================================================
// Logging Configuration
// ============================================================================

// Enable verbose OTA logging
#define OTA_DEBUG_LOGGING 1

// Log memory usage during OTA
#define OTA_LOG_MEMORY 1

// Log HTTP response headers
#define OTA_LOG_HTTP_HEADERS 0

// ============================================================================
// Derived Configuration
// ============================================================================

#ifdef OTA_DEBUG_LOGGING
#define OTA_LOG(fmt, ...) Serial.printf("[OTA] " fmt "\n", ##__VA_ARGS__)
#else
#define OTA_LOG(fmt, ...)
#endif

// ============================================================================
// Example: How to Use These Settings
// ============================================================================

/*
In your code, use the configuration like:

#include "OTAConfig.h"
#include "OTAUpdater.h"

void checkForUpdates() {
  FirmwareInfo info;
  if (OTAUpdater::checkForUpdate(OTA_GITHUB_OWNER, OTA_GITHUB_REPO, info)) {
    Serial.printf("Current: %s, Available: %s\n", FIRMWARE_VERSION, info.version.c_str());

    if (OTAUpdater::performUpdate(info.downloadUrl, info.size)) {
      #if OTA_AUTO_CONFIRM_DELAY_MS > 0
        delay(OTA_AUTO_CONFIRM_DELAY_MS);
        OTAUpdater::confirmBoot();
      #endif
      ESP.restart();
    }
  }
}

// In setup():
void setup() {
  #if OTA_ENABLE_FORCE_PIN
    // Check if OTA force pin is held
    // ...
  #endif

  #if OTA_AUTO_CHECK_ON_BOOT
    // Immediately check for updates
    checkForUpdates();
  #endif

  #if OTA_AUTO_CHECK_PERIODIC
    // Create task to check periodically
    // xTaskCreatePinnedToCore(otaCheckTask, ...);
  #endif
}
*/