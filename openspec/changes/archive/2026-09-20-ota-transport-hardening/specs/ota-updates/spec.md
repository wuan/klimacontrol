## ADDED Requirements

### Requirement: Transport predicates are covered by native tests

The deterministic predicates that gate the OTA transport's observable behavior — version comparison, redirect-scheme classification, GitHub URL composition, GitHub host-allowlist prefix match, and asset-name strict match — SHALL each be exercised by at least one test case in the native PlatformIO test environment (`pio test -e native`).

Each predicate SHALL be reachable from a native test without dragging in `<esp_http_client.h>`. Concretely, predicates that are not currently exposed outside the `#ifdef ARDUINO`-guarded transport code SHALL be moved into a header-only `inline` function in `src/support/` (or a comparable native-buildable location) so they can be included by the test.

#### Scenario: Version comparison rejects downgrades

- **WHEN** the native test suite runs with a running version of `v0.0.74` and an available tag of `v0.0.73`
- **THEN** `Support::isNewerVersion` SHALL return false

#### Scenario: Version comparison accepts unsuffixed dev builds as equal

- **WHEN** the native test suite runs with a running version of `v1.2.3-4-gabc1234` (a `git describe` build four commits past the v1.2.3 tag) and an available tag of `v1.2.3`
- **THEN** `Support::isNewerVersion` SHALL return false

#### Scenario: Version comparison returns 0 on unparseable input

- **WHEN** the native test suite runs with either version not matching `v%d.%d.%d`
- **THEN** `Support::compareVersions` SHALL return 0 (so the caller refuses the update)

#### Scenario: Redirect to absolute https is accepted

- **WHEN** the native test suite runs with a captured `Location` header of `https://release-assets.githubusercontent.com/...`
- **THEN** the redirect-scheme classifier SHALL return true

#### Scenario: Redirect to absolute http is refused

- **WHEN** the native test suite runs with a captured `Location` header of `http://example.com/firmware.bin`
- **THEN** the redirect-scheme classifier SHALL return false

#### Scenario: Relative redirect is accepted

- **WHEN** the native test suite runs with a captured `Location` header of `/path/to/firmware.bin` (relative, inherits the request's scheme)
- **THEN** the redirect-scheme classifier SHALL return true

#### Scenario: GitHub API URL is composed correctly

- **WHEN** the native test suite runs with an owner of `wuan` and a repo of `klimacontrol`
- **THEN** the composed check URL SHALL equal `https://api.github.com/repos/wuan/klimacontrol/releases/latest`

#### Scenario: GitHub host allowlist accepts github.com downloads

- **WHEN** the native test suite runs with a download URL of `https://github.com/wuan/klimacontrol/releases/download/v0.0.74/firmware.bin`
- **THEN** the host-allowlist prefix check SHALL return true

#### Scenario: GitHub host allowlist rejects non-github.com downloads

- **WHEN** the native test suite runs with a download URL of `https://example.com/firmware.bin`
- **THEN** the host-allowlist prefix check SHALL return false

#### Scenario: Asset-name strict match accepts firmware.bin

- **WHEN** the native test suite runs with an asset name of `firmware.bin`
- **THEN** the asset-name strict match SHALL return true

#### Scenario: Asset-name strict match rejects similarly-named assets

- **WHEN** the native test suite runs with an asset name of `littlefs.bin`, `bootloader.bin`, `ota-firmware.bin`, or `firmware.bin.sha256`
- **THEN** the asset-name strict match SHALL return false