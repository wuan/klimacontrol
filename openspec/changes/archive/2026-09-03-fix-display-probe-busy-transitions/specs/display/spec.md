## MODIFIED Requirements

### Requirement: tryBeginForApInfo delegates to the proven init path

`DisplayManager::tryBeginForApInfo(config)` MUST return true when the
e-paper panel is available for AP info rendering, and MUST return false
otherwise. The function MUST be implemented in
`src/display/DisplayManager.{h,cpp}`.

If the manager is already in normal operation (`enabled == true`), the
function SHALL return true without touching anything — the panel is
obviously present and usable.

Otherwise the function SHALL call `panel.probe(timeoutMs)` first and
return false immediately if the probe reports the panel is absent. The
probe path is the BUSY-transition check described under *E-paper panel
can be probed by BUSY pin transition* below; it is a pure presence
check and does NOT touch the SPI bus, the GxEPD2 driver state, or the
`initialised` / `faulted` flags of `EPaperDisplay`.

Only on a successful probe SHALL the function call
`panel.begin(config.rotation)` to bring the panel up via the proven
`GxEPD2::display.init()` path. `panel.begin()` returning false after a
successful probe (a panel that responds to the reset pulse but later
faults on init) SHALL be treated as "no display" and the function
SHALL return false.

A factory-fresh device with `DisplayConfig.enabled == false` and no
panel physically connected therefore still goes through the probe, the
probe correctly observes no BUSY transition, the function returns
false, and the firmware falls back to open AP. A factory-fresh device
with a panel physically connected goes through the probe, observes
both BUSY transitions, calls `panel.begin(0)`, brings the panel up,
and the AP comes up WPA2-PSK with the password on the panel.

The function SHALL be `#ifdef ARDUINO`. The native test build does not
need a stub: `DisplayManager` is itself `#ifdef ARDUINO`, so the
boundary being tested at the native level is `Network::isApOpen()`
returning `true` after a synthetic `startAP()` with no manager wired
in (the native build can model "panel absent" only).

#### Scenario: Healthy panel returns true

- **WHEN** `tryBeginForApInfo()` is called with a connected and
  responsive panel
- **THEN** `panel.probe(timeoutMs)` SHALL observe both BUSY transitions
  and return true; then `panel.begin(0)` SHALL run `GxEPD2::display.init()`
  and return true; the function returns true

#### Scenario: Manager already enabled returns true without re-init

- **WHEN** `tryBeginForApInfo()` is called and `enabled == true`
- **THEN** it returns true without calling `panel.probe()` or
  `panel.begin()`. The panel is already up from `setupDisplay()`, and
  the AP info screen is rendered on top of it via `showApInfo()`

#### Scenario: Stuck or missing panel returns false via the BUSY transition check

- **WHEN** `tryBeginForApInfo()` is called with a panel that fails the
  BUSY-transition check (the BUSY line never goes LOW during the
  manual reset pulse because no panel is wired up, OR BUSY is held LOW
  externally and never releases, OR BUSY is held HIGH externally and
  never transitions) or with no display wired up
- **THEN** `panel.probe(timeoutMs)` SHALL return false; the function
  returns false WITHOUT calling `panel.begin()`. The caller treats a
  false return as "no display detected" and falls back to open AP

#### Scenario: Probe passes but init faults returns false

- **WHEN** `tryBeginForApInfo()` is called and `panel.probe(timeoutMs)`
  returns true but the subsequent `panel.begin(config.rotation)` runs
  `GxEPD2::display.init()` and the fault guard trips
- **THEN** the function returns false. The caller falls back to open
  AP — same outcome as a stuck-or-missing panel
