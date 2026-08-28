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

The firmware-internal `clear_pending` flag SHALL NOT appear in the response and
SHALL NOT be settable through the request body.

`POST /api/display` SHALL require the CSRF header
`X-Requested-With: KlimaControl` via `verifyCsrfHeader()`, SHALL validate the
body through `Config::validateDisplayConfig()`, SHALL persist the result, and
SHALL then call `config.requestRestart(1000)` — matching the save-then-restart
convention of the other settings routes.

When the request disables a display that was previously enabled, the firmware
SHALL additionally persist `clear_pending = true` so the panel is blanked on the
next boot.

#### Scenario: Reading the display configuration

- **WHEN** a client GETs `/api/display` on a device that has never configured the display
- **THEN** the response SHALL be `200` with `enabled: false`, `rotation: 0`, `interval: 60`, and SHALL NOT contain a `clear_pending` key

#### Scenario: Enabling the display

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 2, "interval": 120}` and the CSRF header
- **THEN** the configuration SHALL be persisted and a restart SHALL be scheduled

#### Scenario: Disabling the display arms the blanking

- **WHEN** a client POSTs `/api/display` with `{"enabled": false}` while the display was previously enabled
- **THEN** the persisted configuration SHALL have `enabled = false` and `clear_pending = true`, and a restart SHALL be scheduled

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/display` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written

#### Scenario: Out-of-range values are clamped, not rejected

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 9, "interval": 1}`
- **THEN** the persisted configuration SHALL contain a `rotation` in 0..3 and an `interval` in 10..3600

#### Scenario: The handler does not perform an e-paper refresh

- **WHEN** any `POST /api/display` request is handled
- **THEN** the handler SHALL return without driving the panel, so the AsyncTCP callback is not blocked for the duration of a refresh
