## Why

Two unrelated security findings in `docs/CODE_REVIEW.md` share the same shape:
a user-supplied string is concatenated into a request or a network identity
without ever being validated, so an attacker in radio range or on the LAN can
trick the device into doing work on their behalf.

### #6 — `POST /api/actuator` host string is concatenated into a URL

`routes/ControlRoutes.cpp:298-329` accepts any string in `host`, copies it into
a 64-byte slot via `config.updateActuatorAssignment()`, and the URL is then
constructed at `actuator/HeatingActuator.cpp:38` as `snprintf("http://%s%s",
host, path)`. Every 30 s the Network task probes that URL with
`HTTPClient::GET()` and stores the response on the actuator object, exposed
through `GET /api/actuator`. Anyone on the LAN can `POST /api/actuator` with
`host: "192.168.1.42"` and read whatever that host serves — a printer's admin
page, a router's status page, a stale debug endpoint on a NAS. There is no
authority over what host the device is allowed to talk to, and no upper bound
on what the response can carry.

### #19 — Configuration AP is open

`Network::startAP()` (`Network.cpp:140`) calls `WiFi.softAP(ap_ssid.c_str())`
with no password. Anyone in radio range can associate and `POST /api/settings/wifi`
with attacker-controlled credentials, after which the device joins an
attacker-controlled SSID and its traffic is on the attacker's network. Finding
#19 and finding #6 chain: an attacker that gets past the open AP can submit an
arbitrary actuator host and then read back whatever that host serves.

## What Changes

- A pure-C++ host validator (`src/support/HostValidation.h`) that accepts an
  empty string, a clean IPv4 literal, or `^[A-Za-z0-9._-]{1,253}$` and rejects
  everything else — including any byte outside the printable DNS-name range
  and any of the URL metacharacters `/`, `?`, `#`, `@`, `:`. The validator is
  shared between the route handler (explicit 400 with a machine-readable
  reason) and `updateActuatorAssignment()` (defensive clear on bad input, the
  way the channel check already works).
- A pure-C++ AP-password derivation (`src/support/ApPassword.h`) that maps a
  six-hex device id deterministically to an 8-character WPA2-PSK. The password
  is logged at INFO when AP mode starts so a developer with a serial monitor
  can join without a case label, and broadcast in the AP-mode captive portal
  page itself so a phone does not need to read the boot log. The mapping is
  not cryptographically strong — the MAC is already in the SSID — but it does
  prevent the opportunistic association that an open AP invites, and it
  satisfies finding #19's recommended "WPA2-PSK with derived password" path.
- WPA2-PSK enabled on `WiFi.softAP()` in `Network::startAP()`. The captive
  portal page (settings page rendered in AP mode) gains a one-line note
  showing the SSID and password so a phone user does not need a separate
  reference. Same-page note is the only UI change; no new endpoint.
- Native tests for both validators, covering the SSRF attack matrix and the
  password stability across re-derivation.
- Spec requirements added in `http-api` (host validation rule), `heating-actuator`
  (defense-in-depth at the storage boundary), and `networking` (WPA2-PSK on
  config AP, password derivation rules).

### Non-goals

- Cryptographic protection against an attacker who has read the SSID. The
  password derivation is documented as "raise the bar against opportunistic
  association" rather than "secure against an attacker who knows the device
  id", and the spec says so. Anyone with a screwdriver and a serial cable can
  reflash the device; that is the boundary of the threat model.
- Blocking loopback, link-local, or private-range IP literals. The validator
  permits them because the user's Shelly is on the LAN and may well be in a
  private range, and silently refusing `10.0.0.1` would be the wrong
  surprise. The SSRF concern is satisfied by rejecting URL metacharacters;
  address-range filtering is a different (and separate) policy decision.
- TLS for the actuator RPC. That is finding #18 in `docs/CODE_REVIEW.md` and
  is intentionally out of scope here.
- A `// printed on the case` physical workflow. The firmware logs the
  password and the captive portal shows it on screen. The case label is a
  deployment concern that follows from the firmware side, not a change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `http-api`: add a requirement for `POST /api/actuator` to validate the
  `host` field against `^[A-Za-z0-9._-]{1,253}$` (or an IPv4 literal, or
  empty) and reject URL metacharacters with HTTP 400.
- `heating-actuator`: add a requirement that
  `Config::ConfigManager::updateActuatorAssignment()` defensively re-validates
  the host at the storage boundary, so a bad value cannot reach NVS even if a
  future caller bypasses the route handler.
- `networking`: add a requirement that the configuration AP runs WPA2-PSK
  with a password derived deterministically from the device id, that the
  derivation is pure C++ and testable on native, and that the password is
  visible (logged and shown in the captive portal) so a user without the
  case label can join.

## Impact

- **Source**: `src/support/HostValidation.h` (new),
  `src/support/ApPassword.h` (new), `src/Config.cpp` (defensive host check
  in `updateActuatorAssignment()`), `src/Network.cpp` (WPA2-PSK in
  `startAP()`, log + captive-portal password display),
  `src/routes/ControlRoutes.cpp` (validate host, return 400), and the captive
  portal's settings page in `data/config.html` (one-line note with SSID +
  password).
- **Tests**: `test/test_actuator_host_validation/` and
  `test/test_ap_password/`, both native, asserting the validator/derivation
  functions shape.
- **Spec**: delta in `openspec/changes/<this>/specs/{http-api,heating-actuator,networking}/spec.md`.
- **No NVS schema change**, no firmware version bump.
