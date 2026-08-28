# local-time Specification

## Purpose
TBD - created by archiving change add-timezone-config. Update Purpose after archive.
## Requirements
### Requirement: Timezone is stored as a POSIX TZ string

The firmware SHALL represent the configured timezone as a POSIX TZ string
carrying both the UTC offset and the daylight-saving transition rules, for
example `CET-1CEST,M3.5.0,M10.5.0/3`.

The firmware SHALL NOT store a bare numeric UTC offset, and SHALL NOT maintain
its own table of daylight-saving transition rules.

#### Scenario: Rules travel with the setting

- **WHEN** a timezone whose region observes daylight saving is configured
- **THEN** the stored value SHALL include the transition rules, so no further configuration is needed when the clocks change

#### Scenario: Default is UTC

- **WHEN** a device boots with no timezone stored in NVS
- **THEN** the effective timezone SHALL be `UTC0`, reproducing the pre-existing behaviour of showing UTC

### Requirement: Timezone application

`Support::applyTimezone(const char *tz)` SHALL set the `TZ` environment variable
and call `tzset()`, so that subsequent `localtime_r()` calls resolve in the
configured zone.

The firmware SHALL apply the stored timezone once during `setup()`, before any
component can format a local time.

#### Scenario: Applied at boot

- **WHEN** the device completes configuration load during `setup()`
- **THEN** the stored timezone SHALL have been applied before the network task or the display can render a time

### Requirement: Daylight saving is observed automatically

Conversion from a UTC epoch to local time SHALL honour the daylight-saving rules
in the configured POSIX string, with no user action required at a transition.

#### Scenario: Winter time

- **WHEN** the timezone is `CET-1CEST,M3.5.0,M10.5.0/3` and the epoch is 2026-01-15T12:00:00Z
- **THEN** the local time SHALL be 13:00

#### Scenario: Summer time

- **WHEN** the timezone is `CET-1CEST,M3.5.0,M10.5.0/3` and the epoch is 2026-07-15T12:00:00Z
- **THEN** the local time SHALL be 14:00

#### Scenario: The transition instant

- **WHEN** the timezone is `CET-1CEST,M3.5.0,M10.5.0/3` and the epoch advances from 2026-03-29T00:59:00Z to 2026-03-29T01:00:00Z
- **THEN** the local time SHALL advance from 01:59 to 03:00, skipping the 02:00 hour

#### Scenario: Southern-hemisphere inversion

- **WHEN** the timezone is `AEST-10AEDT,M10.1.0,M4.1.0/3` and the epoch is 2026-01-15T00:00:00Z
- **THEN** the local time SHALL reflect daylight saving being *in* effect, unlike a northern-hemisphere zone at the same instant

### Requirement: Local time formatting helpers

The firmware SHALL provide, in a translation unit free of Arduino and FreeRTOS
dependencies so it builds and is tested in the `native` environment:

- `size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch)` — writes `HH:MM`
- `size_t formatLocalDate(char *out, size_t n, uint32_t epoch)` — writes `YYYY-MM-DD`

Both SHALL write an empty string and return 0 when `epoch` is 0, which is the
established "NTP not yet synced" sentinel. Both SHALL tolerate a null buffer or
a zero-length buffer without writing.

#### Scenario: Unsynced clock renders nothing

- **WHEN** `formatLocalHhMm()` is called with an epoch of 0
- **THEN** it SHALL write an empty string and return 0, rather than rendering the local representation of the Unix epoch

#### Scenario: Null and zero-length buffers

- **WHEN** either formatter is called with a null pointer, or with a buffer length of 0
- **THEN** it SHALL return 0 and SHALL NOT write to memory

### Requirement: Timezone validation

`Support::isPlausibleTimezone(const char *tz)` SHALL reject a null, empty, or
over-long string, and a string containing non-printable characters. It SHALL
accept the full range of ordinary POSIX forms, including quoted designations
(`<+04>-4`) and fractional offsets (`IST-5:30`).

`validateDeviceConfig()` SHALL replace an empty or implausible stored timezone
with `UTC0`, so a corrupted NVS read degrades to UTC rather than to undefined
`tzset()` behaviour.

#### Scenario: Corrupted value degrades to UTC

- **WHEN** the stored timezone is empty or fails the plausibility check
- **THEN** `validateDeviceConfig()` SHALL set it to `UTC0`

#### Scenario: Unusual but valid forms are accepted

- **WHEN** the timezone is `IST-5:30` (fractional offset) or `<+04>-4` (quoted designation)
- **THEN** `isPlausibleTimezone()` SHALL accept it and the offset SHALL be applied correctly

### Requirement: The transported epoch stays UTC

Applying a timezone SHALL affect presentation only. `Network::getCurrentEpoch()`,
the MQTT payload's `time` field, and any epoch exposed through the HTTP API SHALL
continue to carry UTC seconds.

#### Scenario: MQTT payload is unaffected

- **WHEN** a non-UTC timezone is configured and a measurement is published
- **THEN** the `time` field in the MQTT payload SHALL still be the UTC epoch

