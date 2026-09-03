## ADDED Requirements

### Requirement: Actuator host is re-validated at the storage boundary

`Config::ConfigManager::updateActuatorAssignment()` SHALL re-validate the
`actuatorHost` argument with the same character-class predicate the
`POST /api/actuator` route applies (`^[A-Za-z0-9._-]{1,253}$`, empty
permitted) before writing to NVS or to the in-memory `deviceConfig` cache.
A non-empty host that does not match the predicate SHALL be treated as if
no host had been supplied: the stored `deviceConfig.actuator_host` SHALL
be `""` and `deviceConfig.actuator_channel` SHALL be
`ACTUATOR_CHANNEL_UNASSIGNED`. No partial write SHALL occur.

This rule exists so a future caller that bypasses the route handler (a CLI,
a future MQTT control channel, a unit test, etc.) cannot store a host that,
once concatenated into `snprintf("http://%s%s", host, path)` inside
`HeatingActuator::httpGet()`, would redirect the device's actuator probe
away from its configured manifold.

#### Scenario: Storage rejects SSRF payload from a non-HTTP caller

- **WHEN** `ConfigManager::updateActuatorAssignment("192.168.1.42/admin", 0)`
  is called directly (without going through the HTTP route)
- **THEN** the stored `deviceConfig.actuator_host` SHALL be `""` and
  `deviceConfig.actuator_channel` SHALL be `-1`, exactly as if an empty
  host had been passed

#### Scenario: Storage accepts a valid IPv4 literal

- **WHEN** `ConfigManager::updateActuatorAssignment("192.168.1.1", 2)` is
  called
- **THEN** `deviceConfig.actuator_host` SHALL be `"192.168.1.1"` and
  `deviceConfig.actuator_channel` SHALL be `2`

#### Scenario: Storage accepts an mDNS-style hostname

- **WHEN** `ConfigManager::updateActuatorAssignment("shellypro4pm-aabbccddeeff.local", 0)`
  is called
- **THEN** `deviceConfig.actuator_host` SHALL be
  `"shellypro4pm-aabbccddeeff.local"` and `deviceConfig.actuator_channel`
  SHALL be `0`

#### Scenario: Storage rejects whitespace and control bytes

- **WHEN** `ConfigManager::updateActuatorAssignment()` is called with a
  non-empty host containing a space, a tab, a newline, or any byte outside
  `^[A-Za-z0-9._-]$`
- **THEN** the assignment SHALL be cleared and no NVS write SHALL occur for
  the host field

#### Scenario: Round-trip from NVS does not reintroduce a bad host

- **WHEN** `loadDeviceConfig()` reads a stored `actuator_host` that was
  written by a pre-fix firmware and contains characters outside the
  permitted set
- **THEN** `validateDeviceConfig()` (or an equivalent load-time check
  applied to the host field) SHALL clear the host to `""` and the channel
  to `ACTUATOR_CHANNEL_UNASSIGNED`, so the device cannot drive an actuator
  whose URL is malformed

#### Scenario: Empty host from any caller clears the assignment

- **WHEN** `ConfigManager::updateActuatorAssignment("", 2)` or
  `ConfigManager::updateActuatorAssignment(nullptr, 2)` is called
- **THEN** `deviceConfig.actuator_host` SHALL be `""` and
  `deviceConfig.actuator_channel` SHALL be `-1` — the existing "host with
  no channel clears the assignment" behaviour is preserved for empty
  input as well
