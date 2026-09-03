## ADDED Requirements

### Requirement: Configuration AP runs WPA2-PSK, not open

The configuration AP SHALL be brought up with `WiFi.softAP(ssid, password)`
using a WPA2-PSK passphrase, never `WiFi.softAP(ssid)` alone. An open AP
allows anyone in radio range to associate and submit
`POST /api/settings/wifi` with attacker-controlled credentials, after
which the device joins an attacker-controlled SSID. WPA2-PSK raises the
bar against opportunistic association; it is not, on its own, a defence
against an attacker who already knows the SSID — the device id is
broadcast in the SSID and the password is derived from it — and the
design is documented accordingly.

#### Scenario: AP is brought up with WPA2-PSK

- **WHEN** `Network::startAP()` is invoked
- **THEN** it SHALL call `WiFi.softAP(ap_ssid.c_str(), ap_password)`
  where `ap_password` is the 8-character passphrase produced by
  `Support::computeApPassword()`; an over-the-air capture SHALL show
  the AP as WPA2-PSK-encrypted, not as an open network

#### Scenario: Association with no password is rejected

- **WHEN** a client attempts to associate with the AP without supplying
  the password
- **THEN** the association SHALL be rejected by the AP

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
