## ADDED Requirements

### Requirement: OTA source files live under src/ota/

All OTA-related source files — `OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`, `RedirectScheme.h`, and `VersionCompare.h` — SHALL be located under the `src/ota/` directory.

Headers and implementation files that are exclusively consumed by the OTA subsystem SHALL be co-located there rather than living under `src/` top-level or under `src/support/`, so that all OTA implementation lives in one obvious place.

#### Scenario: Listing OTA files

- **WHEN** a reader enumerates the files under `src/ota/`
- **THEN** the directory SHALL contain `OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`, `RedirectScheme.h`, and `VersionCompare.h`, and SHALL NOT be missing any of them

#### Scenario: No OTA files at src/ top level

- **WHEN** a reader enumerates the files directly under `src/`
- **THEN** no file SHALL be named `OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`, `RedirectScheme.h`, or `VersionCompare.h` (i.e. none of the OTA sources are at `src/` root)

#### Scenario: No OTA files under src/support/

- **WHEN** a reader enumerates the files directly under `src/support/`
- **THEN** no file SHALL be named `RedirectScheme.h` or `VersionCompare.h` (i.e. none of the OTA-only support headers remain there)
