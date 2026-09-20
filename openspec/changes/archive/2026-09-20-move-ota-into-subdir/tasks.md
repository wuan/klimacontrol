## 1. Move OTA headers from src/ into src/ota/

- [x] 1.1 Create `src/ota/` directory.
- [x] 1.2 `git mv src/OTAUpdater.h src/ota/OTAUpdater.h` (preserves history).
- [x] 1.3 `git mv src/OTAUpdater.cpp src/ota/OTAUpdater.cpp`.
- [x] 1.4 `git mv src/OTAConfig.h src/ota/OTAConfig.h`.
- [x] 1.5 `git mv src/support/RedirectScheme.h src/ota/RedirectScheme.h`.
- [x] 1.6 `git mv src/support/VersionCompare.h src/ota/VersionCompare.h`.

## 2. Update include directives in OTA files

- [x] 2.1 In `src/ota/OTAUpdater.cpp`, update the `support/RedirectScheme.h` and `support/VersionCompare.h` includes to `ota/RedirectScheme.h` and `ota/VersionCompare.h`. Leave the relative `"OTAUpdater.h"` and `"OTAConfig.h"` includes unchanged (same directory).
- [x] 2.2 `RedirectScheme.h` and `VersionCompare.h` themselves do not include anything, so no further edits inside the moved files.

## 3. Update include directives in OTA consumers

- [x] 3.1 In `src/routes/OTARoutes.cpp`, update `#include "OTAUpdater.h"` to `#include "ota/OTAUpdater.h"` and `#include "OTAConfig.h"` to `#include "ota/OTAConfig.h"`.
- [x] 3.2 In `src/display/EPaperDisplay.cpp`, update `#include "OTAConfig.h"` to `#include "ota/OTAConfig.h"` (uses `FIRMWARE_VERSION`).
- [x] 3.3 In `test/test_ota_transport/test_ota_transport.cpp`, update `#include "OTAConfig.h"` to `#include "ota/OTAConfig.h"`, `#include "support/RedirectScheme.h"` to `#include "ota/RedirectScheme.h"`, and `#include "support/VersionCompare.h"` to `#include "ota/VersionCompare.h"`.
- [x] 3.4 In `test/test_ota_updater/test_ota_updater.cpp`, update `#include "support/VersionCompare.h"` to `#include "ota/VersionCompare.h"`.
- [x] 3.5 In `src/Network.cpp`, update `#include "OTAUpdater.h"` to `#include "ota/OTAUpdater.h"` (uses `OTAUpdater::isUpdateInProgress()`).
- [x] 3.6 In `src/main.cpp`, update `#include "OTAUpdater.h"` to `#include "ota/OTAUpdater.h"` (uses `OTAUpdater::begin()` and `OTAUpdater::confirmRunningImage()`).
- [x] 3.7 Grep the entire `src/` and `test/` trees for the four old paths (`"OTAUpdater.h"`, `"OTAConfig.h"`, `"support/RedirectScheme.h"`, `"support/VersionCompare.h"`) and confirm only the same-directory `"OTAUpdater.h"` and `"OTAConfig.h"` references inside `src/ota/OTAUpdater.cpp` remain.

## 4. Update doc / AGENTS descriptive references

- [x] 4.1 `AGENTS.md` line 101: `src/support/VersionCompare.h` → `src/ota/VersionCompare.h`.
- [x] 4.2 `docs/OTA_QUICK_START.md` line 26: example `#include "OTAUpdater.h"` → `#include "ota/OTAUpdater.h"`.

## 5. Verify

- [x] 5.1 Run `pio test -e native` from the repo root. All 657 tests pass.
- [x] 5.2 Run `pio run -e adafruit_qtpy_esp32s2` from the repo root. Firmware builds with no warnings.
- [x] 5.3 Run `openspec validate --all --strict` from the repo root. All specs and changes pass.
