# network-wifi-resilience Specification

## Purpose
TBD - created by archiving change wifi-resilience. Update Purpose after archive.
## Requirements
### Requirement: Log WiFi events with human-readable reason codes

The system SHALL register a `WiFi.onEvent()` handler in `startSTA()` that logs WiFi station events. Disconnect events SHALL include both the numeric reason code and a translated string label for at least the following codes: `UNSPECIFIED`, `AUTH_EXPIRE`, `AUTH_LEAVE`, `ASSOC_EXPIRE`, `ASSOC_TOOMANY`, `NOT_AUTHED`, `NOT_ASSOCED`, `ASSOC_LEAVE`, `ASSOC_NOT_AUTHED`, `BEACON_TIMEOUT`, `NO_AP_FOUND`, `AUTH_FAIL`, `ASSOC_FAIL`, `HANDSHAKE_TIMEOUT`, `CONNECTION_FAIL`. Unknown reason codes SHALL render as `OTHER` and preserve the numeric value in the same log line.

#### Scenario: AP times out the device

- **WHEN** the AP stops responding and the WiFi stack raises `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` with reason `WIFI_REASON_BEACON_TIMEOUT` (200)
- **THEN** the syslog contains a line of the form `WiFi event: DISCONNECTED reason=200 (BEACON_TIMEOUT)`

#### Scenario: Unknown reason code

- **WHEN** the WiFi stack raises a disconnect event with a reason value not in the recognized set
- **THEN** the log line renders the reason as `OTHER` with the numeric value preserved (e.g. `reason=42 (OTHER)`)

#### Scenario: Successful association

- **WHEN** the station associates with the AP and the WiFi stack raises `ARDUINO_EVENT_WIFI_STA_CONNECTED`
- **THEN** the syslog contains a `WiFi event: STA_CONNECTED` line including the channel number

### Requirement: Retry initial association before declaring failure

`startSTA()` SHALL attempt `WiFi.begin()` up to 3 times. Each attempt SHALL wait up to 15 seconds for `WL_CONNECTED`. Between attempts the system SHALL call `WiFi.disconnect(false)` and wait at least 3 seconds before retrying. The watchdog SHALL be reset inside the wait so the network task does not panic during the retry window. Only after all 3 attempts fail SHALL the caller treat the connection as failed and proceed to the existing failure-counter / restart path.

#### Scenario: First attempt succeeds

- **WHEN** the AP is reachable and association completes within 15 seconds of the first `WiFi.begin()`
- **THEN** no further attempts are made and `startSTA()` returns with `WiFi.status() == WL_CONNECTED`

#### Scenario: Second attempt succeeds after transient failure

- **WHEN** the first attempt times out but the second `WiFi.begin()` associates within 15 seconds
- **THEN** the connection is reported as successful and the device does not restart

#### Scenario: All three attempts fail

- **WHEN** no attempt associates within its 15-second window
- **THEN** `startSTA()` returns with `WiFi.status() != WL_CONNECTED` and the existing boot-time failure-counter logic increments and restarts the device

### Requirement: Recover mid-session disconnects via active reconnect

When the device has been associated and then loses the WiFi connection, the system SHALL NOT rely solely on Arduino's `setAutoReconnect(true)`. After WiFi has been disconnected for at least 30 seconds without recovering, the network task SHALL force a reconnect by calling `WiFi.disconnect(false)` followed by `WiFi.reconnect()`. Forced reconnects SHALL be spaced at least 30 seconds apart. After 6 consecutive forced reconnects without success, the system SHALL call `ESP.restart()` to clear deep stack state. The "disconnected duration" SHALL be measured from a disconnect timestamp captured by the WiFi event callback; if the polling loop observes `WiFi.status() != WL_CONNECTED` after a prior connected state and no event timestamp has been recorded, the polling loop SHALL stamp the disconnect timestamp itself so recovery proceeds even when the underlying event is not delivered.

#### Scenario: Disconnect event is silently dropped

- **WHEN** WiFi transitions from `WL_CONNECTED` to a non-connected status (e.g. `WL_IDLE_STATUS`) without `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` being delivered to the registered handler
- **THEN** on the first poll iteration that observes the missing connection, the network task stamps the disconnect timestamp from `millis()`, and the 30-second active-reconnect timer begins counting from that moment

#### Scenario: Auto-reconnect succeeds within 30 seconds

- **WHEN** WiFi drops and Arduino's auto-reconnect restores the connection within 30 seconds
- **THEN** no forced reconnect is triggered and the active-reconnect failure counter remains 0

#### Scenario: Auto-reconnect silently gives up

- **WHEN** WiFi has been disconnected for 30 seconds and the device is still not associated
- **THEN** the system logs the duration and the last reason, calls `WiFi.disconnect(false)` and `WiFi.reconnect()`, and increments the active-reconnect failure counter

#### Scenario: Forced reconnect succeeds

- **WHEN** a forced reconnect successfully restores the connection (the next poll observes `WL_CONNECTED`)
- **THEN** the active-reconnect failure counter is reset to 0

#### Scenario: Forced reconnect exhausted

- **WHEN** 6 consecutive forced reconnects fail to restore the connection
- **THEN** the system logs `Active reconnect exhausted - restarting` and calls `ESP.restart()`

#### Scenario: Forced reconnect interval enforced

- **WHEN** a forced reconnect has just been triggered
- **THEN** no further forced reconnect is triggered for at least 30 seconds, regardless of how long the connection has been down

