## MODIFIED Requirements

### Requirement: Update progress and outcome reporting

The firmware SHALL expose the state of a background update so that a client can observe progress and learn the outcome. `POST /api/ota/update` returns as soon as the worker is dispatched, so the outcome SHALL NOT be reported only to the log.

`GET /api/ota/update` SHALL report a status of `idle`, `downloading`, `pending`, `success`, or `error`; SHALL include `percent`, `bytes`, and `expected_bytes` while downloading; SHALL include `percent`, `bytes`, and `expected_bytes` during `pending` and `success`; and SHALL include a human-readable `error` message on failure. The `pending` state covers the brief window between `Update.end()` returning success and the scheduled restart taking effect, so a polling client can render the post-flash state as "Update installed, rebooting…" rather than collapsing it into `success`.

#### Scenario: Progress during download

- **WHEN** a download is in progress
- **THEN** `GET /api/ota/update` SHALL respond with `status: "downloading"`, a `percent` value that advances, a `bytes` value that advances, and an `expected_bytes` value matching the size carried by the last successful check

#### Scenario: Pending state is reported after flash completes

- **WHEN** `Update.end()` returns success and before the scheduled restart fires
- **THEN** `GET /api/ota/update` SHALL respond with `status: "pending"`, `percent: 100`, `bytes` equal to the total flashed, and `expected_bytes` equal to `bytes`

#### Scenario: Failure is reported to the client

- **WHEN** the download aborts (connection lost, size mismatch, flash write failure, or insufficient memory)
- **THEN** `GET /api/ota/update` SHALL respond with `status: "error"` and an `error` message describing the cause, and the device SHALL continue running the previous firmware

#### Scenario: Success is reported before the restart

- **WHEN** the scheduled restart is about to fire
- **THEN** `GET /api/ota/update` SHALL respond with `status: "success"` before the device drops off the network

## ADDED Requirements

### Requirement: Consolidated OTA status reporting

`GET /api/ota/status` SHALL report, in addition to its existing `firmware_version`, `build_date`, `build_time`, `partition`, `partition_address`, `free_heap`, `min_free_heap`, `unconfirmed_update`, and `ota_safe` fields, two further fields:

- `check`: an object reporting the background check's state and outcome. Its `state` field SHALL be `idle`, `in_progress`, `done`, or `failed`. On `done` it SHALL include `version`, `size_bytes`, `update_available`, `can_reinstall`, and `is_dev_build_promotion` (the same fields the standalone `GET /api/ota/check` endpoint returns in its `done` branch). On `failed` it SHALL include `error`.
- `update`: an object reporting the background update's state and outcome. Its `state` field SHALL be `idle`, `downloading`, `pending`, `success`, or `error`. On `downloading`, `pending`, and `success` it SHALL include `percent`, `bytes`, and `expected_bytes`. On `error` it SHALL include `error`. (The fields mirror the standalone `GET /api/ota/update` endpoint.)

The consolidated response SHALL NOT replace the standalone endpoints: `GET /api/ota/check` and `GET /api/ota/update` SHALL continue to be exposed with their existing response shapes.

#### Scenario: Consolidated status reports idle state for both check and update

- **WHEN** no check has run and no update has been attempted since boot
- **THEN** `GET /api/ota/status` SHALL respond with `check.state: "idle"` and `update.state: "idle"`

#### Scenario: Consolidated status reports an in-progress check

- **WHEN** a background check is currently running on the worker task
- **THEN** `GET /api/ota/status` SHALL respond with `check.state: "in_progress"`

#### Scenario: Consolidated status reports a completed check

- **WHEN** the background check has finished and the latest release is strictly newer than the running firmware
- **THEN** `GET /api/ota/status` SHALL respond with `check.state: "done"`, `check.version`, `check.size_bytes`, and `check.update_available: true`

#### Scenario: Consolidated status reports a downloading update with byte progress

- **WHEN** a background update is in progress
- **THEN** `GET /api/ota/status` SHALL respond with `update.state: "downloading"`, `update.percent`, `update.bytes`, and `update.expected_bytes`

#### Scenario: Consolidated status reports a pending update after flash

- **WHEN** `Update.end()` has returned success and the device is about to restart
- **THEN** `GET /api/ota/status` SHALL respond with `update.state: "pending"`, `update.percent: 100`, `update.bytes` equal to `update.expected_bytes`

#### Scenario: Standalone endpoints remain available alongside the consolidated response

- **WHEN** `GET /api/ota/status` has been extended with `check` and `update` blocks
- **THEN** `GET /api/ota/check` SHALL continue to respond with the existing `status` / `current_version` / `latest_version` / `update_available` / `can_reinstall` / `is_dev_build_promotion` shape, and `GET /api/ota/update` SHALL continue to respond with the existing `status` / `percent` / `bytes` shape plus the new `expected_bytes` field
