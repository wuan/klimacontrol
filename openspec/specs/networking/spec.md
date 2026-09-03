# networking Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Two network modes

The firmware SHALL support two mutually exclusive network modes: Station (STA) mode for normal operation, and Access Point (AP) mode for initial configuration. The active mode SHALL be selected at boot based on whether WiFi credentials are configured and whether the recent connection-failure count has reached the AP-fallback threshold.

#### Scenario: First boot has no credentials

- **WHEN** the firmware boots and `config.isConfigured()` is false
- **THEN** the Network task SHALL start in AP mode

#### Scenario: Configured device boots into STA

- **WHEN** the firmware boots with stored credentials and the connection-failure count is below the AP-fallback threshold
- **THEN** the Network task SHALL start in STA mode

### Requirement: AP mode setup interface

In AP mode the firmware SHALL broadcast an open SSID of the form `Klima <device-id>` (where `<device-id>` is the device identifier derived from the last three MAC bytes). The AP SHALL use the standard ESP32 SoftAP IP of `192.168.4.1`. The firmware SHALL run a captive portal that redirects DNS queries to itself so a phone that joins the AP is presented with the configuration page automatically.

#### Scenario: Joining the AP

- **WHEN** a client joins the open SSID
- **THEN** the captive portal SHALL answer DNS queries with the AP's IP, causing the operating system's captive-portal probe to direct the user to the configuration page

#### Scenario: Configuration page in AP mode

- **WHEN** the user reaches the configuration page over the AP
- **THEN** the firmware SHALL serve the settings page, accept the WiFi credentials via `POST /api/settings/wifi`, and schedule a restart into STA mode

### Requirement: STA mode association

In STA mode the firmware SHALL associate with the configured SSID using `WiFi.begin(ssid, password)` and SHALL obtain an IP address via DHCP. After successful association the firmware SHALL log SSID, BSSID, channel, RSSI, gateway IP, DNS IP, and configured TX power for diagnostics.

#### Scenario: DHCP-assigned address

- **WHEN** STA mode association succeeds
- **THEN** `WiFi.localIP()` SHALL return a non-zero address obtained via DHCP, and the firmware SHALL log the assigned IP

### Requirement: AP fallback after repeated failures

The firmware SHALL track consecutive WiFi connection failures across reboots in NVS. After every 3rd failure (`failures % 3 == 0`), on the next boot the firmware SHALL open AP mode for at most 5 minutes to allow reconfiguration. If new credentials are received within the AP-fallback window, the failure counter SHALL be reset and the device SHALL restart into STA mode. If the AP-fallback window expires without reconfiguration, the failure counter SHALL be incremented and the device SHALL restart.

#### Scenario: Third consecutive failure

- **WHEN** the firmware boots with a connection-failure count divisible by 3
- **THEN** the Network task SHALL open AP mode and run the captive portal for up to 5 minutes

#### Scenario: User reconfigures during AP fallback

- **WHEN** the user submits new credentials during the AP-fallback window
- **THEN** the failure counter SHALL be reset to 0 and the device SHALL restart immediately

#### Scenario: AP fallback timeout

- **WHEN** the 5-minute AP-fallback window expires without new credentials
- **THEN** the firmware SHALL increment the failure counter (persisted to NVS) and restart, so STA mode is attempted again on the next boot

### Requirement: mDNS advertisement

In both AP and STA modes the firmware SHALL run an mDNS responder, publishing the hostname `klima-<device-id>.local` and advertising an HTTP service on port 80. The mDNS instance name SHALL incorporate the configured `device_name` when one is set, otherwise the `device_id`.

#### Scenario: STA mode discovery

- **WHEN** another host on the LAN queries `klima-<device-id>.local`
- **THEN** the mDNS responder SHALL answer with the device's DHCP-assigned IP address

#### Scenario: Custom device name

- **WHEN** the user has set `device_name` to a non-default value
- **THEN** the mDNS instance name SHALL be `Klima <device_name>`, not `Klima <device-id>`

### Requirement: NTP time synchronization

In STA mode the firmware SHALL synchronize wall-clock time via NTP, using `NTPClient`. The initial sync SHALL be attempted immediately after association via `forceUpdate()`. Once synced, the firmware SHALL refresh the time every 1 hour. If the initial sync fails, the firmware SHALL retry once per minute until the first sync succeeds.

A sync SHALL only count as successful when **both** `forceUpdate()` returns `true` AND the resulting epoch lies in the plausibility range `[NTP_MIN_VALID_EPOCH, NTP_MAX_VALID_EPOCH]` (year 2020 to year 2100 UTC). When `forceUpdate()` returns `true` but the epoch is not plausible, the firmware SHALL treat the sync as failed: emit an `ESP_LOGE` line with the bogus value, increment `ntpBogusSyncCount`, and (depending on the call site) either keep `ntpSynced = false` and let the existing retry timer fire, or keep the previous `lastNtpUpdateEpoch` and `ntpSynced = true` so the device does not flap on a one-off bad refresh.

#### Scenario: First sync after association

- **WHEN** STA association succeeds AND `ntpClient.forceUpdate()` returns `true` AND the resulting `ntpClient.getEpochTime()` lies in the plausibility range
- **THEN** the firmware SHALL record the synced epoch and expose it via `getCurrentEpoch()`

#### Scenario: Bogus epoch is rejected at initial sync

- **WHEN** `ntpClient.forceUpdate()` returns `true` but the resulting epoch is outside the plausibility range (e.g. 0, a small value, or any value > `NTP_MAX_VALID_EPOCH`)
- **THEN** the firmware SHALL NOT set `ntpSynced = true`, SHALL leave `lastNtpUpdateEpoch` unchanged, SHALL emit an `ESP_LOGE` line that includes the bogus value, and SHALL increment `ntpBogusSyncCount`

#### Scenario: Bogus epoch is rejected on a periodic refresh

- **WHEN** the device is already synced and a periodic 1-hour `forceUpdate()` returns `true` but the resulting epoch is outside the plausibility range
- **THEN** the firmware SHALL keep the previous `lastNtpUpdateEpoch` and keep `ntpSynced = true`, SHALL emit an `ESP_LOGE` line that includes the bogus value, SHALL call `reportInternetFailure()`, and SHALL increment `ntpBogusSyncCount`

#### Scenario: Epoch before sync

- **WHEN** NTP has not yet synced
- **THEN** `getCurrentEpoch()` SHALL return 0, even though `NTPClient::getEpochTime()` would otherwise return elapsed-since-boot

### Requirement: Network task blocking-call safety

Every external (non-loopback) network call made from the Network task SHALL either bound its own execution time (via a documented socket, library, or application-level timeout) or feed the FreeRTOS task watchdog (`esp_task_wdt_reset()`) immediately before and after the call. This contract exists so that a hung UDP socket, a stalled TCP connection, or any other blocking call inside the task cannot starve the 30 s task watchdog and force a panic reset that may land inside an NVS write.

The NTP update is the canonical example: `Network::safeNtpUpdate()` SHALL call `esp_task_wdt_reset()` before and after `ntpClient.forceUpdate()` and SHALL return the NTPClient result unchanged. Any future external network call added to the Network task MUST follow the same pattern (helper or inline WDT feeds) and MUST be added to the audit table in the change design.

#### Scenario: NTP call completes normally

- **WHEN** the Network task calls `safeNtpUpdate()` and `ntpClient.forceUpdate()` returns `true` within the library timeout
- **THEN** `safeNtpUpdate()` returns `true` and the watchdog is fed at least twice (before and after the NTP exchange)

#### Scenario: NTP call times out

- **WHEN** the Network task calls `safeNtpUpdate()` and the underlying UDP socket is unreachable so `ntpClient.forceUpdate()` returns `false` after its internal timeout
- **THEN** `safeNtpUpdate()` returns `false` and the watchdog is still fed (before the call and after the call returns)

#### Scenario: NTP call would exceed TWDT budget on a degraded link

- **WHEN** the underlying `WiFiUDP::parsePacket()` blocks for longer than the per-iteration watchdog budget due to lwIP retransmits
- **THEN** the watchdog is fed before the call (resetting the 30 s budget) and again after the call returns, so the panic handler does not fire on a transiently hung link

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

### Requirement: AP password derivation is pure C++ and testable

The device SHALL derive its configuration-AP passphrase deterministically
from the device id. The derivation SHALL be implemented in pure C++ (no
Arduino-only headers), SHALL NOT allocate, SHALL be safe to call from
native tests, and SHALL produce the same byte sequence on native and on
the ESP32 target for the same input. The output SHALL be 8 printable ASCII
characters (lowercase hex, `[0-9a-f]{8}`), fitting inside the WPA2-PSK
8-to-63 character limit, and SHALL be written to the output buffer with
a trailing NUL terminator.

The derivation is not cryptographically strong. The MAC is already
broadcast in the SSID (`Klima <device-id>`), so anyone who can read the
SSID can compute the password. The password protects against opportunistic
association by a passerby who has not seen the SSID, not against a
targeted attacker who has.

#### Scenario: Computation is deterministic

- **WHEN** `Support::computeApPassword("AABBCC", buf, sizeof(buf))` is
  called once and then again with the same arguments
- **THEN** both calls SHALL write the identical 8-byte sequence to `buf`
  (no clock, no random, no global state involved)

#### Scenario: Output is 8 hex chars

- **WHEN** the function is called with any 6-hex-char device id
- **THEN** the output SHALL match `^[0-9a-f]{8}$` exactly, with a trailing
  NUL in the ninth byte

#### Scenario: Output buffer too small is rejected

- **WHEN** `Support::computeApPassword(id, buf, 8)` is called with
  `sizeof(buf) == 8`
- **THEN** the function SHALL leave `buf` unmodified and SHALL NOT write a
  partial password

#### Scenario: Distinct ids produce distinct passwords

- **WHEN** two distinct 6-hex device ids are passed to the function
- **THEN** the two outputs SHALL be different (the FNV-1a mixing does not
  collide on the small sample of ids the firmware actually generates)

### Requirement: AP password is discoverable without the case label

The user joining the configuration AP SHALL be able to discover the WPA2
password without needing the device's case label or a separate
reference. The firmware SHALL satisfy both of:

1. Log the SSID and the derived password at `ESP_LOGI` (or the local
   equivalent) when `Network::startAP()` runs, so a developer with a
   serial monitor can read the password from the boot log.
2. Render the SSID and password on the captive portal page itself (the
   settings page served in AP mode), so a phone user does not need a
   separate document. The password SHALL be computed in the handler that
   renders the page and SHALL NOT travel via a query parameter or be
   stored in NVS.

The captive portal block is server-rendered HTML, not a separate HTTP
request, so an attacker who is not yet on the AP cannot read it.

#### Scenario: Password is in the boot log

- **WHEN** the firmware boots into AP mode and the serial monitor is
  attached
- **THEN** the boot log SHALL contain an `ESP_LOGI` line whose body
  identifies the AP SSID and the derived password

#### Scenario: Password is on the captive portal page

- **WHEN** a client joined to the configuration AP loads the settings page
  served by the captive portal
- **THEN** the rendered HTML SHALL display the AP SSID and the derived
  password without requiring a second request, a query parameter, or any
  client-side JavaScript

#### Scenario: Password is not exposed via a GET endpoint in STA mode

- **WHEN** the device is in STA mode and a LAN client requests any URL
- **THEN** no response body SHALL contain the derived AP password (the
  password is only rendered on the captive portal page in AP mode)

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

