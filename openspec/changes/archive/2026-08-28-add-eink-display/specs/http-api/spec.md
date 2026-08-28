## ADDED Requirements

### Requirement: Display endpoints

The firmware SHALL expose `GET /api/display` and `POST /api/display` for reading
and updating the e-paper display configuration, implemented in
`src/routes/DisplayRoutes.cpp` following the structure of the existing
`SyslogRoutes.cpp`.

`GET /api/display` SHALL return the user-facing fields only:

```json
{"enabled": false, "rotation": 0, "interval": 60}
```

`POST /api/display` SHALL require the CSRF header
`X-Requested-With: KlimaControl` via `verifyCsrfHeader()`, SHALL validate the
body through `Config::validateDisplayConfig()`, SHALL persist the result, and
SHALL then call `config.requestRestart(1000)` — matching the save-then-restart
convention of the other settings routes.

When the request disables a display that was previously enabled, the handler
SHALL blank the panel synchronously before saving and scheduling the restart.
This blocks the callback for the duration of a full refresh (~2.6 s), and up to
a further refresh if the Network task is mid-repaint.

#### Scenario: Reading the display configuration

- **WHEN** a client GETs `/api/display` on a device that has never configured the display
- **THEN** the response SHALL be `200` with `enabled: false`, `rotation: 0`, and `interval: 60`

#### Scenario: Enabling the display

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 2, "interval": 120}` and the CSRF header
- **THEN** the configuration SHALL be persisted and a restart SHALL be scheduled

#### Scenario: Disabling the display blanks the panel inline

- **WHEN** a client POSTs `/api/display` with `{"enabled": false}` while the display was previously enabled
- **THEN** the panel SHALL be blanked before the response is sent, the persisted configuration SHALL have `enabled = false`, and a restart SHALL be scheduled

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/display` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written

#### Scenario: Out-of-range values are clamped, not rejected

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 9, "interval": 1}`
- **THEN** the persisted configuration SHALL contain a `rotation` in 0..3 and an `interval` in 10..3600

#### Scenario: Enabling or reconfiguring does not drive the panel

- **WHEN** a `POST /api/display` request enables the display or changes only rotation or interval
- **THEN** the handler SHALL return without driving the panel; only the disable transition performs a blanking refresh
