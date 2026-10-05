#pragma once

// Host stand-in for src/ota/OTAConfig.h. FIRMWARE_VERSION is passed on the
// compiler command line, mirroring scripts/get_version.py's build flag.

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v0.0.0-dev"
#endif
