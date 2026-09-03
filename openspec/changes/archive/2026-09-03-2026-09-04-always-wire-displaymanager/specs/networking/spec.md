## MODIFIED Requirements

### Requirement: Configuration AP runs WPA2-PSK, not open

The configuration AP SHALL run with `WiFi.softAP(ssid, password)` (WPA2-PSK)
when an e-paper panel responds to the probe at AP-mode entry, and SHALL
fall back to `WiFi.softAP(ssid)` (open AP) when no panel responds.

**The probe is the security decision.** It runs inside `Network::startAP()`,
not at boot, so STA-mode boots do not pay the detection cost. The probe
path is `DisplayManager::tryBeginForApInfo()`, which delegates to
`panel.begin()` (which calls `GxEPD2::display.init()` — the proven
init sequence on the Waveshare 1.54" V2). A healthy panel returns true
within ~1 s; a stuck or disconnected panel returns false after
`GxEPD2::faulted` is set, and the firmware falls back to open AP.

**`Network::startAP()` requires `display != nullptr`.** The
`DisplayManager` pointer is installed into Network by
`setupDisplay()` unconditionally — both when the normal status
display is enabled (after `displayManager.begin()` succeeds) and
when it is disabled (so the deferred probe can still bring the
panel up at AP-mode entry). A device with `DisplayConfig.enabled
== false` and a panel physically connected still uses WPA2-PSK,
because the panel is brought up on demand at AP-mode entry.

The MAC is already broadcast in the SSID (`Klima <device-id>`), so
anyone who can read the SSID can compute the password from the
device id (see `Support::computeApPassword`). The password therefore
defends against opportunistic association by a passerby who has not
seen the SSID, not against a targeted attacker.

#### Scenario: Panel responds at AP-mode entry

- **WHEN** `Network::startAP()` is called, `Network::display` is
  non-null, and the e-paper panel responds to the init sequence
  (BUSY transitions through LOW to HIGH, no fault guard trips)
- **THEN** `WiFi.softAP(ap_ssid.c_str(), ap_password)` SHALL be
  called, where `ap_password` is the 8-character hex passphrase
  produced by `Support::computeApPassword()`. The password SHALL be
  rendered on the e-paper panel during AP mode via
  `DisplayManager::showApInfo()`

#### Scenario: DisplayConfig disabled but panel responds

- **WHEN** `DisplayConfig.enabled == false` in NVS, a panel is
  physically connected, and `Network::startAP()` is called
- **THEN** the deferred probe in `tryBeginForApInfo()` SHALL bring
  the panel up on demand (because `enabled == false` in
  DisplayManager, so the probe path runs `panel.begin()` rather
  than short-circuiting). On success the AP comes up WPA2-PSK
  with the password on the panel

#### Scenario: No panel responds at AP-mode entry

- **WHEN** `Network::startAP()` is called and no e-paper panel
  responds to the init sequence (panel faulted, BUSY stuck LOW past
  the timeout, or no DisplayManager is wired)
- **THEN** `WiFi.softAP(ap_ssid.c_str())` SHALL be called with no
  password. The configuration AP is open and any client in radio
  range can associate without a challenge

#### Scenario: Probe does not run at boot

- **WHEN** `setup()` brings up the network task in STA mode
- **THEN** no e-paper probe runs. `Network::apPassword` is never
  read or written. Boot cost in STA mode is unaffected by the
  panel probe

#### Scenario: Probe is re-run on every AP-mode entry

- **WHEN** `Network::startAP()` is called multiple times in a boot
  (cold-boot first-WiFi path AND every-third-failed-reconnection
  fallback path)
- **THEN** the probe runs on each entry. A panel that was responsive
  on the first entry may not be responsive on the second (e.g.,
  interrupted by a WiFi connect attempt in between); the firmware
  falls back to open AP on the second entry
