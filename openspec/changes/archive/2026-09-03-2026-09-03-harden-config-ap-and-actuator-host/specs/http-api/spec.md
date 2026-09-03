## ADDED Requirements

### Requirement: Actuator host string is validated before storage

The `POST /api/actuator` handler SHALL validate the `host` field before
calling `Config::ConfigManager::updateActuatorAssignment()`. An empty `host`
SHALL be accepted (the existing convention for clearing a half-configured
assignment). A non-empty `host` SHALL match the pattern
`^[A-Za-z0-9._-]{1,253}$` — the DNS-name character set with underscores
permitted, length capped at the DNS label maximum — to reject URL
metacharacters (`/`, `?`, `#`, `@`, `:`), whitespace, and any non-printable
byte that would let `snprintf("http://%s%s", host, path)` in
`HeatingActuator::httpGet()` be redirected away from the configured manifold.

A rejected host SHALL cause the handler to respond HTTP 400 with a JSON body
carrying `"success": false` and a `"host"`-naming error message. The stored
`DeviceConfig.actuator_host` SHALL remain unchanged, no NVS write SHALL be
performed, and no `HeatingActuator::configure()` call SHALL be triggered.

`Config::ConfigManager::updateActuatorAssignment()` SHALL additionally
re-validate the host at the storage boundary using the same predicate, so a
bad value cannot reach NVS even if a future caller bypasses the route
handler. The existing "host with no channel clears the assignment" shape
is preserved: bad host, missing host, or invalid host all clear the
assignment rather than storing a half-bad value.

The IPv6 address family is rejected by the same character-class check
(`[`, `]`, `:` are not in the permitted set); no separate IPv6 check is
required. Address-range filtering (private vs public, loopback, etc.) is
intentionally not part of this rule — the Shelly is on the LAN and may
legitimately be in a private range.

#### Scenario: SSRF via userinfo is rejected

- **WHEN** `POST /api/actuator` is sent with body
  `{"host": "192.168.1.42@evil.example.com", "channel": 0}`
- **THEN** the endpoint SHALL respond with HTTP 400, SHALL NOT call
  `updateActuatorAssignment()`, and the stored `actuator_host` SHALL be
  unchanged

#### Scenario: SSRF via path is rejected

- **WHEN** `POST /api/actuator` is sent with body
  `{"host": "192.168.1.42/admin", "channel": 0}`
- **THEN** the endpoint SHALL respond with HTTP 400 and SHALL NOT modify
  `actuator_host`

#### Scenario: IPv6 literal is rejected

- **WHEN** `POST /api/actuator` is sent with body
  `{"host": "fe80::1", "channel": 0}` or `{"host": "[fe80::1]", "channel": 0}`
- **THEN** the endpoint SHALL respond with HTTP 400

#### Scenario: Empty host is accepted and clears the assignment

- **WHEN** `POST /api/actuator` is sent with body `{"host": "", "channel": -1}`
- **THEN** the stored `actuator_host` SHALL be `""`, the stored
  `actuator_channel` SHALL be `ACTUATOR_CHANNEL_UNASSIGNED`, and the
  endpoint SHALL respond with HTTP 200

#### Scenario: Typical IPv4 and hostnames are accepted

- **WHEN** `POST /api/actuator` is sent with body
  `{"host": "192.168.1.1", "channel": 0}` or
  `{"host": "shellypro4pm-aabbccddeeff.local", "channel": 0}` or
  `{"host": "klima-01.lan", "channel": 0}`
- **THEN** the endpoint SHALL respond with HTTP 200 and the stored
  `actuator_host` SHALL be the exact string submitted

#### Scenario: Storage boundary rejects bad host from a non-HTTP caller

- **WHEN** `ConfigManager::updateActuatorAssignment()` is called directly
  with a non-empty host string containing `/` (or any other
  character outside the permitted set) and a valid channel
- **THEN** the assignment SHALL be cleared: `deviceConfig.actuator_host`
  SHALL be `""` and `deviceConfig.actuator_channel` SHALL be
  `ACTUATOR_CHANNEL_UNASSIGNED`, regardless of what the caller passed

#### Scenario: Host longer than 253 characters is rejected

- **WHEN** `POST /api/actuator` is sent with a `host` string longer than
  253 characters
- **THEN** the endpoint SHALL respond with HTTP 400
