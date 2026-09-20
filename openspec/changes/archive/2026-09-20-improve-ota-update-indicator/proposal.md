## Why

The settings page's OTA progress display is plain text: "Installing update… 35%". During the 1–3 minute download there is no visual feedback beyond a changing number, no byte count, and no indication of what the device is actually doing (downloading, flashing, about to reboot). The settings page is also silent during the 5–30 s background check — there is no way to tell a hung check from a still-running one. The reference implementation at github.com/oetztal/ledz solves all three with a real progress bar, a bytes-written/total counter, state-aware labels, and a consolidated status endpoint that reports both check and update state in one response.

## What Changes

- **Progress UI**: `data/settings.html` gains a real `<progress>`-style bar (CSS-driven width transition), a bytes-written/total counter, and a state-aware label that renders "Downloading firmware…", "Update installed, rebooting…", and "Update failed" depending on the device-reported state.
- **Status badge**: the firmware-version info box gains a persistent `check=… update=…` line so a stuck or in-progress check is visible at a glance, not only after it finishes.
- **Consolidated status endpoint**: `GET /api/ota/status` is extended to also return `check` and `update` state blocks (mirroring `/api/ota/check` and `/api/ota/update`). The frontend's two separate poll loops collapse into one 1 s poll of `/api/ota/status`.
- **`Pending` state**: `OTAUpdater` gains a `Pending` state set after `Update.end()` returns success and before `config.requestRestart()` fires, so the UI can label the ~1 s "flashed, about to reboot" gap honestly. `Pending` is reported on `/api/ota/update` (and on the consolidated `update` block) alongside the existing `Downloading`, `Success`, and `Failed` states.
- **`expected_bytes` field**: `GET /api/ota/update` (and the consolidated `update` block) gains an `expected_bytes` field while downloading and on success, sourced from the size carried by the last successful check. This is what the byte counter reads.
- **Polling cadence**: the consolidated poll runs at 1 s during an active update for a smoother bar (was 2 s); falls back to 5 s when idle.

The existing endpoints (`/api/ota/check`, `/api/ota/update`) and existing fields (`bytes`) remain — the new state and field are additive, no consumer breaks.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `ota-updates`: adds the `Pending` state to the update state machine, the `expected_bytes` field on the update status response, and the consolidation of check/update state onto `GET /api/ota/status`.

## Impact

- **Firmware**: `src/ota/OTAUpdater.h` (add `Pending` to the `UpdateState` enum), `src/ota/OTAUpdater.cpp` (`setUpdateState(Pending, …)` between `Update.end()` success and `config.requestRestart()`), `src/routes/OTARoutes.cpp` (expose `Pending` on `GET /api/ota/update`, expose `expected_bytes`, fold check+update state into `GET /api/ota/status`).
- **Frontend**: `data/settings.html` (progress bar markup + CSS, bytes counter, state-aware label, always-visible status badge, single-poll wiring).
- **Tests**: no native-test surface area changes — the `Pending` state is gated on `#ifdef ARDUINO` (the chunk loop and `Update.begin/end` are).
- **Spec**: `openspec/specs/ota-updates/spec.md` gains scenarios for `Pending`, `expected_bytes`, and the consolidated status endpoint.
- **Active changes**: `add-heating-cutover-runbook` and `assess-display-brownout-risk` are unrelated to OTA — no merge conflict.
- **Backwards compatibility**: additive — no field is renamed, no endpoint is removed, no state is replaced. Clients of `/api/ota/check` and `/api/ota/update` continue to work.
