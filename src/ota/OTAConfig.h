//
// OTA Configuration
//
// Only settings that are actually consumed by OTAUpdater live here. Anything
// that is not referenced from code has been removed: this file used to carry a
// dozen knobs (chunk size, HTTP timeout, heap floor, LED feedback, auto-check
// intervals) that OTAUpdater duplicated as its own private constants, so
// tuning them here had no effect whatsoever.
//

#pragma once

#include <cstring>

// ============================================================================
// GitHub Configuration
// ============================================================================

// Your GitHub username/organization
#define OTA_GITHUB_OWNER "wuan"

// Your repository name
#define OTA_GITHUB_REPO "klimacontrol"

// Host used to look up the latest release metadata. Centralised so the check
// URL is composed from one constant rather than a string literal buried in
// the call site.
#define OTA_GITHUB_API_HOST "https://api.github.com/"

// Host that serves release asset downloads (github.com then 302-redirects to
// its signed CDN). The first-hop URL must start with this prefix; subsequent
// hops are guarded by the redirect-scheme classifier.
#define OTA_GITHUB_RELEASE_HOST "https://github.com/"

// Exact name of the release asset holding the application image. Matched
// exactly, not by ".bin" suffix: a release also carrying a filesystem image, a
// bootloader blob, or a build for another board must never have one of those
// flashed as the application. Produced by .github/workflows/release.yml.
#define OTA_FIRMWARE_ASSET "firmware.bin"

namespace Support {
    /**
     * True iff `name` is the exact release asset filename this firmware will
     * flash. Strict match (==), not suffix / prefix — see the comment on
     * `OTA_FIRMWARE_ASSET` for why. Pure C++, no Arduino-only headers, so it
     * can be exercised from a native test without dragging in <esp_http_client.h>.
     */
    inline bool isExpectedFirmwareAsset(const char *name) {
        if (name == nullptr) {
            return false;
        }
        return strcmp(name, OTA_FIRMWARE_ASSET) == 0;
    }
} // namespace Support

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
