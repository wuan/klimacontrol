## Context

The settings page's OTA progress UI shows only "Installing update… 35%" during the multi-minute download — a number that ticks up with no other visual feedback, no byte count, and no indication of what the device is doing. The page is also silent during the 5–30 s background check; there is no way to distinguish "still working" from "stuck". The reference implementation at github.com/oetztal/ledz resolves both with a CSS progress bar driven by an animated width transition, a bytes-written/total counter, state-aware labels, and a consolidated status endpoint that reports both check and update state in a single response.

On the firmware side, the only meaningful gap in the state machine is the ~1 s window between `Update.end()` returning success and `config.requestRestart()` actually rebooting the device. The current code collapses that into the same `Success` state as the rest of the post-flash path, so a client polling right after the flash completes sees "success" — which is technically true but reads to a user as "done, nothing more to do", missing the brief "device is about to drop off the network" message.

## Goals / Non-Goals

**Goals:**
- A visual progress bar with smooth width transitions during the download.
- A bytes-written / bytes-total counter alongside the percent.
- A state-aware label that reads as "Downloading firmware…" / "Update installed, rebooting…" / "Update failed" depending on the device's actual state.
- A persistent `check=… update=…` line in the firmware-version info box so background check progress is visible at a glance.
- A consolidated status endpoint that surfaces both check and update state in a single response so the UI needs one poll loop, not two.
- A new `Pending` state in `OTAUpdater` for the ~1 s post-flash window so the label can read honestly.

**Non-Goals:**
- No change to the underlying OTA transport (the existing `ESP-IDF esp_http_client` + `Update.begin/write/end` path stays).
- No rename of existing fields or removal of existing endpoints (`/api/ota/check`, `/api/ota/update`, `bytes`, `percent`, the four-state set `idle/downloading/success/failed` all remain).
- No ETA / throughput display (the reference doesn't have it either; this keeps the spec and UI lean).
- No separate `Flashing` state. With chunk-by-chunk write, `Update.end()` runs in milliseconds; a state visible only for that window is dead surface.
- No change to polling cadence for `/api/ota/check` or `/api/ota/update` themselves — the new consolidated `/api/ota/status` polls at 1 s during an active update and 5 s when idle.
- No native-test additions — `Pending` and the bytes counter are gated on `#ifdef ARDUINO` (the chunk loop and `Update.begin/end` are), so the existing native test surface is unaffected.

## Decisions

### Decision 1: Add `Pending` as a new `UpdateState` rather than repurposing `Success`

`Success` currently fires once, immediately after `Update.end()` returns. With `Pending` between `Update.end()` and `config.requestRestart()`, the state transitions cleanly:
`Downloading` → `Pending` → `Success` → (device reboots) → `Idle`.

The `Pending` window is ~1 s — long enough for the UI to poll it once at 1 s cadence. The label "Update installed, rebooting…" is honest about what the device is doing. Reusing `Success` would have meant the label read "Update installed" for the entire post-flash path, including the moment the device is mid-reboot and any further requests will fail with connection refused.

### Decision 2: `Pending` is set *after* `Update.end()` returns, *before* `config.requestRestart()` is called

Placing the `setUpdateState(Pending, 100, totalRead, nullptr)` call between those two points maximizes the polling window. `Update.end()` is the seal-and-validate step; once it returns, the firmware has committed to booting from the new partition, so "Pending" is true at that instant. `config.requestRestart(1000)` schedules the restart 1 s out — the worker thread continues running between `Update.end()` and the actual reboot, so polling sees `Pending` for that window.

The worker also still calls `setUpdateState(Success, …)` after `config.requestRestart()` to preserve the existing endpoint contract — this is what `pollOtaUpdate` already treats as "done, device is restarting".

### Decision 3: `/api/ota/status` becomes the consolidation point; the per-endpoint endpoints stay

`GET /api/ota/status` already returns `firmware_version`, `build_date`, `partition`, `free_heap`, `unconfirmed_update`, `ota_safe`. Adding `check` and `update` blocks to this response lets the frontend replace its two poll loops (`pollOtaCheck` at 1.5 s, `pollOtaUpdate` at 2 s) with one consolidated poll. The existing `/api/ota/check` and `/api/ota/update` endpoints stay so any other consumer (CLI tool, future mobile app, the existing `pollOtaCheck` until the UI is updated) keeps working.

The consolidation is purely additive: `data.check` and `data.update` are new fields on the existing response. No existing field is removed or renamed.

### Decision 4: `expected_bytes` is exposed via the `update` block; `bytes` stays as-is

The user explicitly waved off field-naming concerns, so the minimum-diff option wins: keep `bytes`, add `expected_bytes` alongside it. `expected_bytes` is the size that came from the last successful check (already known to the firmware via `FirmwareInfo::size`), and is reported during `Downloading`, `Pending`, and `Success`. On `Idle` and `Failed` it's `0`.

We considered renaming `bytes` → `bytes_written` for symmetry with `expected_bytes` (the LEDz convention). Rejected: nothing downstream consumes `bytes_written` that wouldn't also consume `expected_bytes`, but renaming `bytes` would force the existing UI code to update in lockstep with the firmware — one more place to forget. Keeping `bytes` and adding `expected_bytes` is additive and grep-discoverable.

### Decision 5: `data.check` carries only what the consolidated poll needs

The consolidated `data.check` block on `/api/ota/status` includes:
- `state`: one of `idle` / `in_progress` / `done` / `failed`
- On `done`: `version`, `size_bytes`, `is_dev_build_promotion`, `can_reinstall`, `update_available`
- On `failed`: `error`

It does *not* include `current_version` (already at the top of the response), `latest_version` (renamed to `version` for terseness — the only consumer is the same UI), or anything else. The standalone `/api/ota/check` endpoint continues to return its existing shape including `latest_version`; only the consolidated block uses the shorter names.

### Decision 6: Progress-bar CSS lives inline in `data/settings.html`

The progress bar is only used on one page (settings) and is part of the OTA concern that already lives inline as `<style>` blocks on individual pages (see `data/control.html:44-127`). Promoting `.progress-bar` to `data/common.css` would force a common-css review for what's a single-page concern. Inline matches the reference (LEDz does it inline too) and keeps the diff localized to `settings.html`.

### Decision 7: Polling cadence is 1 s during an active update, 5 s when idle

LEDz polls every 1 s unconditionally. We can do better: when nothing is happening (idle check + idle update) there is no need to hammer the device. The consolidated poller chooses 1 s if `data.update.state` is `downloading` or `pending` (smooth bar matters here) or `data.check.state` is `in_progress` (the user clicked Check and expects feedback); otherwise 5 s. This matches the existing behavior of `pollOtaCheck` (1.5 s) and `pollOtaUpdate` (2 s) in spirit, but coalesces them.

### Decision 8: Status badge is always visible in the firmware-version info box

The line `check=idle update=idle 0%` is rendered even when nothing is happening. This is the LEDz pattern. The benefit: a hung check (server-side timeout, network black hole) is visible to the user without them having to click "Check for Updates". The cost: a permanent extra line of small text in the info box. We accept the cost.

## Risks / Trade-offs

- **Polling 1 s during a download doubles the request volume vs the existing 2 s loop.** The update lasts 1–3 minutes; doubling the request rate for that window is well under any meaningful threshold on the AsyncTCP event task. No mitigation needed.

- **`/api/ota/status` is now a "richer" response and could grow.** This change doesn't make the response materially larger (~6 new fields, all small), but it does establish a precedent for the endpoint growing. The standalone `/api/ota/check` and `/api/ota/update` endpoints remain available for consumers that want a narrow response.

- **`Pending` is only visible for ~1 s before the device drops off the network.** A user who navigates away from the page and back during that window might miss the label. The HTML cache-and-show-on-revisit behavior handles this — if the page loads while the device is gone, the bar's last-known state is `Pending`, which is honestly what happened. No mitigation.

- **The UI's two existing poll loops are removed and replaced with one consolidated loop.** This is a frontend-only refactor; the firmware change is purely additive. If we discover a regression we can fall back to the two-loop pattern by reverting the HTML/JS changes only — the firmware stays.

- **Inline CSS for the progress bar doesn't reuse the design system.** Acceptable because the bar appears on a single page. If a second page ever needs the same component, we'd promote it to `common.css` then.

- **The `expected_bytes` field is reported as 0 during `Failed` state, which could mislead a UI that computes "completed / expected" for a failed run.** Mitigation: the UI's `Failed` branch reads `data.update.error` and shows the error string instead of the byte counter — the byte counter is hidden in that branch.

## Migration Plan

This change is purely additive on both the firmware and frontend sides. There is no migration step. Existing clients of `/api/ota/check`, `/api/ota/update`, and the original `/api/ota/status` fields continue to work unchanged. Rollback is a single revert of the firmware (one enum value, one `setUpdateState` call, one route handler change) and the HTML file (the `updateProgress` block and the consolidated poll script).

Deploy order:
1. Land the firmware change first (adds `Pending`, `expected_bytes`, and the `check`/`update` blocks on `/api/ota/status`). Old firmware can already answer the new fields; no firmware-side coordination needed.
2. Land the frontend change (use the new fields; switch to the consolidated poll). The frontend will keep working against old firmware — the new `check`/`update` blocks will be missing, and the consolidated poller falls back to the legacy behavior of treating `/api/ota/status` as a status-only endpoint.

## Open Questions

None. All design decisions resolved with defaults from the explore discussion.
