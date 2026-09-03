## ADDED Requirements

### Requirement: Configuration AP probes the panel on every factory-fresh boot

The configuration AP SHALL probe for an e-paper panel inside
`Network::startAP()` on every entry — including the very first boot
where no NVS WiFi configuration exists — and SHALL run WPA2-PSK with the
password shown on the panel when one responds.

The probe path is `DisplayManager::tryBeginForApInfo(config)`. It returns
`true` when the manager is already in normal operation (`enabled == true`)
or when a fresh `panel.begin(config.rotation)` succeeds, and `false`
otherwise. A factory-fresh device with `DisplayConfig.enabled == false` in
NVS therefore still benefits from the probe — the manager is not enabled,
so `tryBeginForApInfo` falls through to `panel.begin()`, and a connected
panel brings the AP up WPA2-PSK with the password on the panel.

#### Scenario: Factory-fresh boot with a connected panel

- **WHEN** the firmware boots for the first time (no NVS WiFi config, no
  NVS display config — `DisplayConfig.enabled == false` is the spec
  default), a panel is physically wired up to the SPI connector, and
  `Network::startAP()` runs
- **THEN** `DisplayManager::tryBeginForApInfo()` SHALL call
  `panel.begin(0)` (the default rotation), the panel SHALL respond, and
  the AP SHALL come up WPA2-PSK with the password rendered on the panel
  via `showApInfo()`. The user has to enable the display via the web UI
  before the next STA-mode boot to see the normal status display, but
  AP-mode WiFi credentials are discoverable on the panel from the very
  first second

#### Scenario: Factory-fresh boot with no panel

- **WHEN** the firmware boots for the first time and no panel is
  physically wired up
- **THEN** `DisplayManager::tryBeginForApInfo()` SHALL return `false`
  (the GxEPD2 init writes silently succeed but the panel fault guard
  trips, or `isInitialised()` stays false on a truly absent connector),
  and the AP SHALL come up open. The user can configure WiFi from a
  phone over the open AP — the only configuration path that works on a
  device with no serial cable, no case label, and no panel