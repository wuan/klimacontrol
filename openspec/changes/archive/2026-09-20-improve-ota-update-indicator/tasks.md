## 1. Firmware state machine

- [x] 1.1 Add `Pending` to `OTAUpdater::UpdateState` enum in `src/ota/OTAUpdater.h` with a comment explaining the ~1 s window between `Update.end()` success and the scheduled restart
- [x] 1.2 In `OTAUpdater::otaWorkerTask` (`src/ota/OTAUpdater.cpp`), after `performUpdate()` returns true, call `setUpdateState(UpdateState::Pending, 100, totalRead, nullptr)` before `pendingConfig->requestRestart(...)` so the consolidated poller can observe the post-flash window

## 2. Firmware API additions

- [x] 2.1 In `GET /api/ota/update` handler (`src/routes/OTARoutes.cpp`), expose `Pending` as `status: "pending"` with `percent: 100` and the full byte counts
- [x] 2.2 In the same handler, add `expected_bytes` to the `Downloading`, `Pending`, and `Success` responses (sourced from the size captured by the last successful check; 0 for `Idle` and `Failed`)
- [x] 2.3 In `GET /api/ota/status` handler, add `check` block: `{ state, version, size_bytes, update_available, can_reinstall, is_dev_build_promotion, error }` — reading from `OTAUpdater::getCheckResult(...)` and translating the state names
- [x] 2.4 In `GET /api/ota/status` handler, add `update` block: `{ state, percent, bytes, expected_bytes, error }` — reading from `OTAUpdater::getUpdateProgress(...)` and translating the state names
- [x] 2.5 Confirm `GET /api/ota/check` and `GET /api/ota/update` continue to expose their existing response shapes unchanged

## 3. Frontend markup and styling

- [x] 3.1 In `data/settings.html`, replace the static "Installing update…" text inside `#updateProgress` with a state-aware `<strong id="progressLabel">`, a CSS progress bar (`<div class="progress-bar"><div id="progressBar"></div></div>`), and a `<div id="progressBytes">` for the byte counter
- [x] 3.2 Add inline `<style>` for `.progress-bar` (grey track, animated green fill, ~18 px tall, rounded corners, ~0.3 s width transition) inside the OTA section's `<style>` block, or scoped under `#updateProgress`
- [x] 3.3 In the `#otaCurrentVersion` info box, add a `<span id="otaStateBadge">` that renders the persistent `check=… update=… N%` line
- [x] 3.4 Add a `<div id="updateResult">` reset path so the failed-update branch hides the byte counter (only the error string is shown)

## 4. Frontend JS wiring

- [x] 4.1 Replace the two separate poll loops (`pollOtaCheck`, `pollOtaUpdate`) with one `pollOtaStatus` loop that hits `GET /api/ota/status` every 1 s while an update or check is active and every 5 s otherwise
- [x] 4.2 In the consolidated poller, update the status badge (`#otaStateBadge`) on every poll with `check=<state> update=<state> <percent>%`
- [x] 4.3 In the consolidated poller, update `#progressBar.style.width` to `data.update.percent + '%'` while `data.update.state` is `downloading`
- [x] 4.4 In the consolidated poller, update `#progressBytes.textContent` to `${bytes} / ${expected_bytes} bytes` while `data.update.state` is `downloading`, `pending`, or `success`
- [x] 4.5 In the consolidated poller, set `#progressLabel.textContent` based on `data.update.state`: "Downloading firmware…" / "Update installed, rebooting…" / "Update failed: …" / "Update complete"
- [x] 4.6 Wire `checkForUpdates()` and `performUpdate()` to consume the new `data.check` and `data.update` blocks instead of the old `pollOtaCheck` / `pollOtaUpdate` responses (the existing branch logic stays; only the field references change)

## 5. Validation

- [x] 5.1 Run `pio run -e adafruit_qtpy_esp32s2` and confirm clean compile of `OTAUpdater`, `OTARoutes`, and the compressed `settings.html`
- [x] 5.2 Run `pio test -e native` and confirm no native-test regressions (the change should not affect the native test surface)
- [x] 5.3 Run `openspec validate improve-ota-update-indicator --strict` and confirm the delta applies cleanly
- [x] 5.4 On-device smoke test: trigger a check, observe the persistent status badge transitioning from `check=in_progress` to `check=done`; trigger an install, observe the progress bar advancing, the byte counter, the state label transition Downloading → Pending → Success, and the device restart at the end
