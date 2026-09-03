## MODIFIED Requirements

### Requirement: Configuration AP runs WPA2-PSK, not open

The configuration AP SHALL run with `WiFi.softAP(ssid, password)` (WPA2-PSK)
when an e-paper panel is detected at boot, and SHALL fall back to
`WiFi.softAP(ssid)` (open AP) when no panel is detected. A panel is
"detected" when the BUSY pin transitions HIGH-then-LOW within a reset
pulse budget of 250 ms — see `display` → "E-paper panel can be probed by
BUSY pin transition" for the predicate.

The previously shipped unconditional-WPA2-PSK behaviour is replaced by
this display-conditional rule. The previous behaviour could lock out a
device with no serial cable and no case label: the password is
deterministic from the device id but the id is broadcast in the SSID,
which the user already sees, so the password itself was unreachable
without a serial monitor or a printed case label. Opening the AP when
no display is detected is the only path that works in that case.

#### Scenario: Display detected at boot

- **WHEN** `EPaperDisplay::probe(250)` returns true at boot (BUSY pin
  shows the panel responding to reset)
- **THEN** the firmware SHALL call `WiFi.softAP(ap_ssid.c_str(), ap_password)`
  on the next AP-mode entry, where `ap_password` is the 8-character
  hex passphrase produced by `Support::computeApPassword()`. The
  password SHALL be rendered on the e-paper panel during AP mode — see
  the `display` capability's "AP info screen renders SSID, password,
  IP" requirement

#### Scenario: No display detected at boot

- **WHEN** `EPaperDisplay::probe(250)` returns false at boot (no BUSY
  transition observed within 250 ms)
- **THEN** the firmware SHALL call `WiFi.softAP(ap_ssid.c_str())` on
  the next AP-mode entry, with no password. The configuration AP is
  open and any client in radio range can associate without a challenge

#### Scenario: Probe is held for the boot lifetime

- **WHEN** the probe has run once at boot and made its decision
- **THEN** the decision SHALL NOT be re-evaluated later in the same
  boot, even if the panel is disconnected or the user toggles
  `DisplayConfig.enabled` via the web UI. Mid-run hardware faults are
  not configuration state

## ADDED Requirements

### Requirement: AP security decision is communicated before the network task starts

`main.cpp::setup()` SHALL run the e-paper probe and call
`Network::setApPassword(password)` before `Network::startTask()` is
invoked. The decision SHALL be `password = Support::computeApPassword(deviceId)`
when the probe succeeded, and `password = ""` (open AP) when it failed.
`Network::startAP()` SHALL consult this decision and SHALL NOT re-probe
or re-decide at AP-mode entry.

#### Scenario: Decision is set before network task starts

- **WHEN** `setup()` reaches the line that calls `Network::setApPassword(...)`
- **THEN** the call SHALL complete before `Network::startTask()` is
  invoked, so the network task sees the decision at its first AP-mode
  entry without further work

#### Scenario: Empty password means open AP

- **WHEN** `Network::setApPassword("")` was called
- **THEN** the next call to `Network::startAP()` SHALL bring up
  `WiFi.softAP(ssid)` with no password argument, producing an open AP
