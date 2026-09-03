## ADDED Requirements

### Requirement: E-paper panel can be probed by BUSY pin transition

`Display::EPaperDisplay` SHALL expose a static `probe(uint32_t timeoutMs)`
function that detects whether an e-paper panel is physically present on
the display connector. The probe SHALL:

1. Configure CS, DC, RST as outputs and BUSY as input, with CS held HIGH
   (deselected) and RST starting HIGH.
2. Drive the panel reset sequence: RST HIGH → LOW → HIGH, with at least
   10 ms in each state.
3. Watch the BUSY line. Return true only if BUSY transitions HIGH
   (panel acknowledges reset) within the timeout, and then transitions
   LOW (reset complete) within the same timeout.
4. Return false if either transition is missed, or if the total elapsed
   time exceeds `timeoutMs`.

The probe SHALL NOT modify the GxEPD2 driver state, the SPI bus, or the
internal `initialised` / `faulted` flags of `EPaperDisplay`. It is a
pure presence check.

The probe SHALL be `#ifdef ARDUINO`. The native test build provides a
stub that returns false (display detection is hardware-only).

#### Scenario: Healthy panel returns true

- **WHEN** the BUSY pin goes HIGH within ~100 ms of the reset pulse and
  then LOW within ~100 ms of that
- **THEN** `probe(timeoutMs)` returns true

#### Scenario: Missing panel returns false

- **WHEN** the BUSY pin does not transition within `timeoutMs` (the
  connector is empty, the panel is unpowered, or the BUSY line is
  damaged)
- **THEN** `probe(timeoutMs)` returns false. The caller treats a false
  return as "no display present" and falls back to whatever the
  absence-of-display policy is for the use case (e.g. open AP for the
  configuration network)

#### Scenario: BUSY stuck HIGH returns false

- **WHEN** the BUSY pin is held HIGH externally (interference, damaged
  panel)
- **THEN** the probe sees the HIGH transition but never sees the LOW
  transition within the timeout, and returns false. A false return on
  a stuck-HIGH pin is the safer failure mode (the panel is treated as
  absent rather than as present), since the alternative would lock the
  user out

#### Scenario: BUSY stuck LOW returns false

- **WHEN** the BUSY pin is held LOW externally (interference, damaged
  panel) or floats LOW when no panel is connected
- **THEN** the probe never sees the HIGH transition, and returns false
  on the timeout. A false return is again the safer failure mode

### Requirement: AP info screen renders SSID, password, IP

`Display::EPaperDisplay::showApInfo(ssid, password, ip)` SHALL render a
dedicated AP-mode screen on the panel: the SSID of the configuration
AP, the WPA2-PSK passphrase, and the AP's IP address (typically
`192.168.4.1` for the ESP32 SoftAP). The screen SHALL be a full-window
refresh and SHALL use the same fonts and header band as `showSplash` —
the brand mark at the top, then a three-line body with one labelled
field per line (SSID, Password, IP). The watchdog SHALL be fed before
and after the blocking draw loop, matching the rule in
`system-architecture` → "Network task blocking-call safety".

The method SHALL be a no-op if `initialised == false` or `faulted == true`.

`DisplayManager` SHALL expose `showApInfo(ssid, password, ip)` and
`endApInfo()` wrappers around the panel methods. `showApInfo` SHALL set
an `apModeActive` flag so the normal `update()` tick does not paint
temperature on top of the password screen; `endApInfo` SHALL clear the
flag and SHALL hibernate the panel only if the manager inited the panel
itself (i.e. `enabled == false`). A panel in normal operation is left
alone; STA mode's normal updates will resume after the user submits WiFi
credentials and the device restarts.

#### Scenario: showApInfo paints the three fields

- **WHEN** `DisplayManager::showApInfo("Klima AABBCC", "abcdef01", "192.168.4.1")`
  is called with the panel initialised
- **THEN** the panel SHALL show, in order, the SSID `Klima AABBCC`, the
  passphrase `abcdef01`, and the IP `192.168.4.1`, with the brand mark
  at the top of the panel

#### Scenario: apModeActive suppresses normal updates

- **WHEN** `DisplayManager::apModeActive == true`
- **THEN** `DisplayManager::update()` SHALL return without painting,
  regardless of whether `enabled == true` and a refresh policy says so.
  The AP info screen stays on the panel until `endApInfo()` is called
  or the device restarts into STA mode

#### Scenario: endApInfo hibernates only the manager-init panel

- **WHEN** `endApInfo()` is called on a manager that init'd the panel
  itself (i.e. `enabled == false`)
- **THEN** the panel SHALL be put into deep sleep via
  `EPaperDisplay::hibernate()`, retaining the AP info image

- **WHEN** `endApInfo()` is called on a manager that was already
  enabled (`enabled == true`)
- **THEN** the panel SHALL NOT be hibernated. The next STA-mode
  `update()` tick will resume painting the normal status display,
  overwriting the AP info with the current measurements
