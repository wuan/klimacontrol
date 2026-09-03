## Why

On first boot (no NVS WiFi config) and on every-third-failure AP fallback, the
device enters AP mode for configuration. The configuration AP should run
WPA2-PSK when an e-paper panel is physically connected — the panel then
renders the password so the user can join — and should fall back to open AP
otherwise.

Today, `Network::startAP()` keys the security decision off
`DisplayManager::isEnabled()`, which is the *config* flag
(`DisplayConfig.enabled`). On a factory-fresh device that flag is `false`
(the spec's default), so `startAP()` logs `Display disabled in config — AP
will be open` and brings the AP up open even when a panel is sitting on the
SPI connector. The user has to enable the display via the web UI before
*the next* AP-mode entry to get WPA2-PSK — a chicken-and-egg situation on a
device with no serial cable, no case label, and no panel currently showing
anything.

The display spec already describes a deferred probe inside `startAP()` via
`DisplayManager::tryBeginForApInfo()`, and the archived change
`2026-09-04-probe-deferred-to-ap-mode` designed that path end-to-end. The
code was never written: `tryBeginForApInfo()` is missing from
`DisplayManager.h/cpp`, `Network::startAP()` does not call it, and the
config-flag check that ships today is the placeholder that took its place.
This change ships the missing piece.

## What Changes

- **Add `DisplayManager::tryBeginForApInfo(config)`** in
  `src/display/DisplayManager.{h,cpp}`. Returns `true` immediately when the
  manager is already in normal operation (`enabled == true`); otherwise
  calls `panel.begin(config.rotation)` (which runs `GxEPD2::display.init()`
  — the proven init path on the Waveshare 1.54" V2) and returns its result.
  Delegates to `panel.begin()` rather than the hand-rolled BUSY-pin probe
  in `EPaperDisplay::probe()` — the hand-rolled probe timed out on real
  panels, the proven init path is the right thing to delegate to.
- **Replace the config-flag branch in `Network::startAP()`** with a
  `tryBeginForApInfo()` call. On `true` the AP comes up WPA2-PSK and the
  panel renders SSID + password + IP via `showApInfo()` + `endApInfo()` (the
  flow already present in the code today). On `false` the AP comes up
  open, with a log line naming the probe as the source of the decision.
- **Add a native test** `test_display_manager_ap_info_probe` that exercises
  the `tryBeginForApInfo` boundary without the panel hardware: the native
  build of `EPaperDisplay` already returns `false` from `probe()` (the stub
  in `EPaperDisplay.h:188`), and `panel.begin()` is hardware-only too, so
  the native path returns `false` and the test asserts the "open AP" branch
  is taken.

## Capabilities

### New Capabilities

None. The deferred probe and the WPA2-PSK-vs-open decision are already
specified under `networking` and `display`; this change only ships the code
those specs already require.

### Modified Capabilities

- `networking`: replace the current "decision is made off the
  `DisplayConfig.enabled` flag" wording with the deferred-probe wording
  the spec already carries ("the probe runs inside `Network::startAP()`,
  not at boot"). Add a Scenario covering the new "factory-fresh device
  with a panel physically connected" outcome.
- `display`: clarify `tryBeginForApInfo` delegates to `panel.begin()` (the
  proven GxEPD2 init path), and add a Scenario for the
  "DisplayConfig.enabled == false but panel responds" case (which is the
  gap the change closes).

## Impact

- **Source:** `src/display/DisplayManager.{h,cpp}` (new `tryBeginForApInfo`),
  `src/Network.cpp` (replace the `isEnabled()` check with the probe call),
  `openspec/specs/networking/spec.md` (delta), `openspec/specs/display/spec.md`
  (delta).
- **Tests:** new native test `test_display_manager_ap_info_probe.cpp` under
  `test/test_display_manager_ap_info_probe/`.
- **Spec:** delta in `openspec/changes/<this>/specs/networking/spec.md`
  and `openspec/changes/<this>/specs/display/spec.md`. No new capability.
- **No NVS schema change**, no firmware version bump.
- **Boot behaviour:** on the cold-boot first-WiFi path, the AP probe now
  runs inside `startAP()` instead of being skipped. STA-mode boots are
  unaffected (the probe is gated to AP entry).
- **Risk:** `panel.begin()` calls `GxEPD2::display.init()`, which talks to
  the panel on the SPI bus. On a device with no panel wired up the SPI
  writes silently succeed; the subsequent `showApInfo()` is gated on
  `panel.isInitialised()` and is therefore a no-op. A healthy panel
  returns true within ~1 s; a stuck panel trips the BUSY fault guard
  after 3 timeouts, returns false, and the AP opens — same fallback as
  before.