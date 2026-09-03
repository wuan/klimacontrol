## MODIFIED Requirements

### Requirement: Configuration AP runs WPA2-PSK, not open

The configuration AP SHALL run with `WiFi.softAP(ssid, password)` (WPA2-PSK)
when an e-paper panel responds to the probe at AP-mode entry, and SHALL
fall back to `WiFi.softAP(ssid)` (open AP) when no panel responds.

**The probe is the security decision.** It runs inside `Network::startAP()`,
not at boot, so STA-mode boots do not pay the detection cost. The probe
path is `DisplayManager::tryBeginForApInfo()`, which calls
`panel.probe(timeoutMs)` (the BUSY-transition check described under
`display` → *E-paper panel can be probed by BUSY pin transition*) and,
only on a successful probe, calls `panel.begin(config.rotation)` (which
runs `GxEPD2::display.init()` — the proven init sequence on the
Waveshare 1.54" V2). A healthy panel returns true within ~1 s; a stuck
or disconnected panel returns false at the probe step (BUSY never
transitions), the SPI bus is never touched, and the firmware falls back
to open AP.

The probe-then-begin sequence is what catches the no-panel case the
deferred probe was designed for. `panel.begin()` alone is not enough:
`GxEPD2::display.init()` silently succeeds when no panel is connected
because the SPI writes succeed without an actual panel to drive BUSY
LOW, and `panel.begin()` returns `!faulted` where `faulted` only flips
after three consecutive refresh timeouts, never on init. The probe's
manual RST pulse + BUSY-transition check is what makes the absent-panel
case observable.

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
  non-null, and the e-paper panel responds to the manual RST pulse in
  the probe (BUSY transitions through LOW to HIGH, both within
  `timeoutMs`)
- **THEN** `tryBeginForApInfo()` SHALL pass the probe, call
  `panel.begin(0)`, and return true. `WiFi.softAP(ap_ssid.c_str(),
  ap_password)` SHALL be called, where `ap_password` is the
  8-character hex passphrase produced by
  `Support::computeApPassword()`. The password SHALL be rendered on
  the e-paper panel during AP mode via
  `DisplayManager::showApInfo()`

#### Scenario: DisplayConfig disabled but panel responds

- **WHEN** `DisplayConfig.enabled == false` in NVS, a panel is
  physically connected, and `Network::startAP()` is called
- **THEN** the deferred probe in `tryBeginForApInfo()` SHALL run
  `panel.probe(timeoutMs)` (because `enabled == false` in
  DisplayManager, so the probe path runs rather than the
  short-circuit). The probe SHALL observe both BUSY transitions,
  `panel.begin(0)` SHALL bring the panel up, and the AP comes up
  WPA2-PSK with the password on the panel

#### Scenario: No panel responds at AP-mode entry

- **WHEN** `Network::startAP()` is called and no e-paper panel
  responds to the manual RST pulse in the probe (BUSY stays HIGH
  because the connector is empty, or BUSY is stuck LOW because of a
  damaged panel, or BUSY is stuck HIGH because of interference), or
  no DisplayManager is wired
- **THEN** `panel.probe(timeoutMs)` SHALL return false,
  `tryBeginForApInfo()` SHALL return false without calling
  `panel.begin()`, and `WiFi.softAP(ap_ssid.c_str())` SHALL be
  called with no password. The configuration AP is open and any
  client in radio range can associate without a challenge

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
