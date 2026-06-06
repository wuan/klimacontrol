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

#### Scenario: First sync after association

- **WHEN** STA association succeeds and `ntpClient.forceUpdate()` returns true
- **THEN** the firmware SHALL record the synced epoch and expose it via `getCurrentEpoch()`

#### Scenario: Epoch before sync

- **WHEN** NTP has not yet synced
- **THEN** `getCurrentEpoch()` SHALL return 0, even though `NTPClient::getEpochTime()` would otherwise return elapsed-since-boot

