## MODIFIED Requirements

### Requirement: tryBeginForApInfo delegates to the proven init path

`DisplayManager::tryBeginForApInfo(config)` MUST return true when the
e-paper panel is available for AP info rendering, and MUST return false
otherwise. The function MUST be implemented in
`src/display/DisplayManager.{h,cpp}`.

If the manager is already in normal operation (`enabled == true`), the
function SHALL return true without touching anything — the panel is
obviously present and usable.

Otherwise the function SHALL call `panel.begin(config.rotation)` and
return its result. `panel.begin()` runs `GxEPD2::display.init()` — the
same init path `EPaperDisplay::begin()` uses in normal operation — which
is the proven method on the Waveshare 1.54" V2 and similar panels. A
healthy panel returns true within ~1 s. A panel that faults
(`GxEPD2::faulted` set after 3 consecutive BUSY timeouts) or that has no
display wired up returns false; the firmware falls back to open AP in
that case.

The function SHALL NOT use the hand-rolled BUSY-pin probe (a manual
`RST` pulse followed by polling for `BUSY` transitions) — that sequence
did not reliably drive the SSD1681 through its full init cycle on the
Waveshare 1.54" V2. The GxEPD2 init path is the right thing to delegate
to here.

The function SHALL be `#ifdef ARDUINO`. The native test build does not
need a stub: `DisplayManager` is itself `#ifdef ARDUINO`, so the
boundary being tested at the native level is `Network::isApOpen()`
returning `true` after a synthetic `startAP()` with no manager wired in
(the native build can model "panel absent" only).

#### Scenario: Healthy panel returns true

- **WHEN** `tryBeginForApInfo()` is called with a connected and
  responsive panel
- **THEN** it returns true after `GxEPD2::display.init()` completes

#### Scenario: Manager already enabled returns true without re-init

- **WHEN** `tryBeginForApInfo()` is called and `enabled == true`
- **THEN** it returns true without calling `panel.begin()`. The panel
  is already up from `setupDisplay()`, and the AP info screen is
  rendered on top of it via `showApInfo()`

#### Scenario: Stuck or missing panel returns false

- **WHEN** `tryBeginForApInfo()` is called with a panel that fails to
  init (BUSY stuck LOW past the timeout, fault guard trips) or with no
  display wired up
- **THEN** it returns false. The caller treats a false return as "no
  display detected" and falls back to open AP