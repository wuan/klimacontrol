## MODIFIED Requirements

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

## ADDED Requirements

### Requirement: tryBeginForApInfo delegates to the proven init path

`DisplayManager::tryBeginForApInfo` MUST return true when the
e-paper panel is available for AP info rendering, and MUST return
false otherwise.

If the manager is already in normal operation (`enabled == true`),
the function SHALL return true without touching anything — the
panel is obviously present and usable.

Otherwise the function SHALL call `panel.begin(config.rotation)` and
return its result. `panel.begin()` runs `GxEPD2::display.init()` — the
same init path `EPaperDisplay::begin()` uses in normal operation —
which is the proven method on the Waveshare 1.54" V2 and similar
panels. A healthy panel returns true within ~1 s. A panel that
faults (`GxEPD2::faulted` set after 3 consecutive BUSY timeouts) or
that has no display wired up returns false; the firmware falls back
to open AP in that case.

The function SHALL NOT use the hand-rolled BUSY-pin probe (a
manual `RST` pulse followed by polling for `BUSY` transitions) —
that sequence did not reliably drive the SSD1681 through its full
init cycle on the Waveshare 1.54" V2. The GxEPD2 init path is the
right thing to delegate to here.

#### Scenario: Healthy panel returns true

- **WHEN** `tryBeginForApInfo()` is called with a connected and
  responsive panel
- **THEN** it returns true after `GxEPD2::display.init()` completes

#### Scenario: Manager already enabled returns true without re-init

- **WHEN** `tryBeginForApInfo()` is called and `enabled == true`
- **THEN** it returns true without calling `panel.begin()`. The
  panel is already up from `setupDisplay()`, and the AP info screen
  is rendered on top of it via `showApInfo()`

#### Scenario: Stuck or missing panel returns false

- **WHEN** `tryBeginForApInfo()` is called with a panel that fails
  to init (BUSY stuck LOW past the timeout, fault guard trips) or
  with no display wired up
- **THEN** it returns false. The caller treats a false return as
  "no display detected" and falls back to open AP
