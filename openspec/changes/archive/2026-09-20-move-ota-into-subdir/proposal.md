## Why

OTA-related code is currently scattered across the top of `src/` and into
`src/support/`. `OTAUpdater.h/.cpp`, `OTAConfig.h`, and the two OTA-only
support headers (`RedirectScheme.h`, `VersionCompare.h`) all live next to
unrelated core files. With the OTA subsystem now growing a native test
suite and an extracted `Support::isExpectedFirmwareAsset` helper, the
scatter makes it harder to see at a glance what belongs to OTA and what
belongs to the rest of the firmware. A dedicated `src/ota/` subdirectory
mirrors the convention already used for `src/control/`, `src/sensor/`,
`src/actuator/`, `src/display/`, `src/network/`, and `src/support/`.

## What Changes

- Move `src/OTAUpdater.h`, `src/OTAUpdater.cpp`, `src/OTAConfig.h`,
  `src/support/RedirectScheme.h`, `src/support/VersionCompare.h` into
  `src/ota/`.
- Update include paths in:
  - `src/ota/OTAUpdater.cpp` (relative includes stay the same; `support/`
    includes become `ota/`)
  - `src/routes/OTARoutes.cpp` (`"OTAUpdater.h"`, `"OTAConfig.h"` →
    `"ota/OTAUpdater.h"`, `"ota/OTAConfig.h"`)
  - `src/display/EPaperDisplay.cpp` (`"OTAConfig.h"` →
    `"ota/OTAConfig.h"`, used only for `FIRMWARE_VERSION`)
  - `test/test_ota_transport/test_ota_transport.cpp` (the three OTA
    includes)
  - `test/test_ota_updater/test_ota_updater.cpp` (the
    `support/VersionCompare.h` include)
  - `test/test_actuator_host_validation/` and any other test that
    transitively includes the moved headers via `OTAUpdater.h`
- No header-guard renames (`KLIMACONTROL_*` already match the filename
  root, not the path) and no public API changes.
- `platformio.ini` does not need updating: the `native` build filter
  does not list `OTAUpdater.cpp` (it is `#ifdef ARDUINO`-guarded), and
  PlatformIO auto-discovers `test/*/` directories.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

None. This is a pure refactor; observable behaviour is identical. The
existing `ota-updates` spec already pins down the predicates and the
asset-name strict match — those don't change.

## Impact

- **Code:** five files moved, three to four source files and two test
  files get updated `#include` paths. No `.cpp` bodies change.
- **APIs:** none. `OTAUpdater.h`'s public surface is unchanged.
- **Build:** `pio run -e adafruit_qtpy_esp32s2` and `pio test -e native`
  must both continue to pass with no warnings.
- **Dependencies:** none.
- **Runtime behaviour:** identical. No code paths change; only include
  resolution.