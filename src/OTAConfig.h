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

// ============================================================================
// GitHub Configuration
// ============================================================================

// Your GitHub username/organization
#define OTA_GITHUB_OWNER "wuan"

// Your repository name
#define OTA_GITHUB_REPO "klimacontrol"

// Exact name of the release asset holding the application image. Matched
// exactly, not by ".bin" suffix: a release also carrying a filesystem image, a
// bootloader blob, or a build for another board must never have one of those
// flashed as the application. Produced by .github/workflows/release.yml.
#define OTA_FIRMWARE_ASSET "firmware.bin"

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
