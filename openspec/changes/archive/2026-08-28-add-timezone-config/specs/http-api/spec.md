## ADDED Requirements

### Requirement: Timezone endpoints

The firmware SHALL expose `GET /api/settings/timezone` returning the configured
POSIX TZ string together with the current local time, and
`POST /api/settings/timezone` to update it.

```json
{"timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "local_time": "14:32", "synced": true}
```

`local_time` SHALL be an empty string when NTP has not yet synced, and `synced`
SHALL report that state, so the UI can distinguish "no clock yet" from midnight.

`POST` SHALL require the CSRF header `X-Requested-With: KlimaControl`, SHALL
reject an implausible value with 400, SHALL persist the value, and SHALL apply it
immediately via `Support::applyTimezone()`.

Unlike the other mutating settings routes, this endpoint SHALL NOT schedule a
restart: `tzset()` fully applies the change, and no component holds derived
state that a restart would reconcile.

#### Scenario: Reading the timezone before NTP sync

- **WHEN** a client GETs `/api/settings/timezone` on a device that has not yet synced NTP
- **THEN** the response SHALL contain the configured `timezone`, an empty `local_time`, and `synced: false`

#### Scenario: Updating the timezone takes effect without a restart

- **WHEN** a client POSTs `{"timezone": "CET-1CEST,M3.5.0,M10.5.0/3"}` with the CSRF header
- **THEN** the value SHALL be persisted and applied immediately, no restart SHALL be scheduled, and a subsequent GET SHALL report a `local_time` in the new zone

#### Scenario: Rejecting an implausible value

- **WHEN** a client POSTs an empty or non-printable timezone string
- **THEN** the request SHALL be rejected with 400 and the stored value SHALL be unchanged

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/settings/timezone` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written
