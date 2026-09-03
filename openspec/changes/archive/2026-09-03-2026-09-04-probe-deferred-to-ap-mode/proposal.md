## Why

The previous change (`2026-09-04-ap-password-via-display`) ran the
e-paper probe during `setup()`, before the network task started, and
held the decision (WPA2-PSK vs open AP) in `Network::apPassword` for
the rest of the boot. The probe was correct in principle but paid its
cost on every boot — STA-mode boots (the common case) ran a probe
they didn't need.

AP mode is rare on this device: it is entered only on first boot
without WiFi credentials, or every third failed reconnection. The
probe's only consumer is `Network::startAP()`. Moving the probe into
`startAP()` pays the cost only when the decision actually matters.

## What Changes

- `Network::startAP()` runs the e-paper panel probe at AP-mode entry
  via `DisplayManager::tryBeginForApInfo()`. If the panel responds,
  it derives the WPA2-PSK passphrase from the device id and brings
  the AP up with `WiFi.softAP(ssid, password)`. If the probe does not
  respond, it brings the AP up open (the only configuration path
  that works for a device with no serial cable, no case label, and no
  display).
- The probe block in `main.cpp::setup()` is removed. `setup()` no
  longer reads `Display::EPaperDisplay`, computes the AP password, or
  calls `Network::setApPassword()`. Boot cost in STA mode drops to
  zero for the panel probe.
- `Network::setApPassword()` / `getApPassword()` and the
  `Network::apPassword` member are removed — no caller remains.
- `Support::setApPasswordSlot()` / `getApPasswordSlot()` and their
  tests in `test_ap_password` are removed — the password is now
  computed on demand from the device id, no slot needed.
- `Network::isApOpen()` is added so `/api/ap-info` (which lives in
  the CONFIG route set and therefore runs only when the AP is up)
  can render the captive portal page correctly. The endpoint computes
  the password on demand via `Support::computeApPassword`; it is
  deterministic, so no state needs to be carried from `startAP()`.
- `DisplayManager::tryBeginForApInfo()` no longer uses the
  hand-rolled `panel.probe(250)` BUSY-pin probe (which timed out on
  the Waveshare 1.54" V2). It calls `panel.begin()` directly, which
  uses `GxEPD2::display.init()` — the same proven init path
  `EPaperDisplay::begin()` uses in normal operation.

### Non-goals

- A pre-flight probe for `WiFi.softAPgetStationNum()` or other runtime
  signals. The probe at AP-mode entry is the only detection the
  project needs.
- A second probe on first-boot-with-no-WiFi only. The probe runs on
  every `startAP()` call (cold-boot path AND every-third-failure
  fallback path); both are AP-mode entries and both need the same
  decision.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `networking`: change the "AP security decision is communicated
  before the network task starts" requirement to "the AP security
  decision is made inside `Network::startAP()` at AP-mode entry,
  using the same e-paper probe as before".
- `display`: clarify that `tryBeginForApInfo()` delegates to
  `panel.begin()` (GxEPD2's proven init path), not the hand-rolled
  BUSY-pin probe.

## Impact

- **Source:** `src/Network.{h,cpp}` (probe moved into `startAP`,
  `isApOpen()` added, `setApPassword`/`getApPassword`/`apPassword`
  removed), `src/display/DisplayManager.cpp` (`tryBeginForApInfo`
  simplified), `src/WebServerManager.cpp` (`/api/ap-info` computes
  password on demand), `src/main.cpp` (probe block removed,
  includes cleaned up).
- **Tests:** removed the slot-helper tests in `test_ap_password`
  (helpers no longer exist). Test count drops from 559 to 547.
- **Spec:** delta in
  `openspec/changes/<this>/specs/networking/spec.md` and
  `display/spec.md`.
- **No NVS schema change**, no firmware version bump.
- **Boot behaviour:** unchanged in functional terms — the same
  decision (WPA2-PSK vs open) is still made for every AP-mode
  entry. The decision just happens later (at the entry, not at
  boot), so STA-mode boots no longer pay the probe cost.
