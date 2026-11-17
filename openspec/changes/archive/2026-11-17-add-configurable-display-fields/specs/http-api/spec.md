# Spec Delta — http-api

## MODIFIED Requirements

### Requirement: Display endpoints

The firmware SHALL expose `GET /api/display` and `POST /api/display` for reading
and updating the e-paper display configuration, implemented in
`src/routes/DisplayRoutes.cpp` following the structure of the existing
`SyslogRoutes.cpp`.

`GET /api/display` SHALL return the user-facing fields only:

```json
{"enabled": false, "rotation": 0, "interval": 60,
 "warn_frost_c": null, "warn_humidity_pct": 0,
 "second_field": "humidity", "third_field": "none"}
```

A `null` `warn_frost_c` SHALL mean the frost warning is disabled; a
`warn_humidity_pct` of `0` SHALL mean the humidity warning is disabled.

`second_field` SHALL always report one of `humidity`, `dew_point`, `pressure`,
`sea_level_pressure`, `co2`, `illuminance`, `voc_index` or `pm25`.
`third_field` SHALL report one of those tokens or `none`.

`POST /api/display` SHALL require the CSRF header
`X-Requested-With: KlimaControl` via `verifyCsrfHeader()`, SHALL validate the
body through `Config::validateDisplayConfig()`, SHALL persist the result, and
SHALL then call `config.requestRestart(1000)` — matching the save-then-restart
convention of the other settings routes.

An absent `second_field` or `third_field` key SHALL leave the stored value
unchanged. A present but unknown token SHALL fall back to the documented default
during validation rather than reject the request. `third_field: null` SHALL be
treated as `none`.

When the request disables a display that was previously enabled, the handler
SHALL blank the panel synchronously before saving and scheduling the restart.
This blocks the callback for the duration of a full refresh (~2.6 s), and up to
a further refresh if the Network task is mid-repaint.

#### Scenario: Reading the display configuration

- **WHEN** a client GETs `/api/display` on a device that has never configured the display
- **THEN** the response SHALL be `200` with `enabled: false`, `rotation: 0`, `interval: 60`, `warn_frost_c: null`, `warn_humidity_pct: 0`, `second_field: "humidity"`, and `third_field: "none"`

#### Scenario: Setting display value fields

- **WHEN** a client POSTs `/api/display` with `{"second_field": "dew_point", "third_field": "pressure"}` and the CSRF header
- **THEN** the configuration SHALL be persisted with dew point second and pressure third, and a restart SHALL be scheduled

#### Scenario: Selecting no third field

- **WHEN** a client POSTs `/api/display` with `{"third_field": "none"}` and the CSRF header
- **THEN** the persisted third field SHALL be none and the device SHALL use the two-line display after restart

#### Scenario: Unknown value token falls back

- **WHEN** a client POSTs `/api/display` with an unknown `second_field` or `third_field` token and the CSRF header
- **THEN** the persisted configuration SHALL use the documented default for that field rather than rejecting the request

#### Scenario: Omitting a value field preserves it

- **WHEN** a client POSTs `/api/display` with only `enabled` and `interval` while a second field is already stored
- **THEN** the stored second field SHALL remain unchanged

#### Scenario: Enabling the display

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 2, "interval": 120}` and the CSRF header
- **THEN** the configuration SHALL be persisted and a restart SHALL be scheduled

#### Scenario: Setting warning thresholds

- **WHEN** a client POSTs `/api/display` with `{"warn_frost_c": 5, "warn_humidity_pct": 70}` and the CSRF header
- **THEN** the configuration SHALL be persisted with both thresholds enabled and a restart SHALL be scheduled

#### Scenario: Disabling a threshold warning

- **WHEN** a client POSTs `/api/display` with `{"warn_frost_c": null, "warn_humidity_pct": 0}` and the CSRF header
- **THEN** both threshold warnings SHALL be disabled in the persisted configuration

#### Scenario: Disabling the display blanks the panel inline

- **WHEN** a client POSTs `/api/display` with `{"enabled": false}` while the display was previously enabled
- **THEN** the panel SHALL be blanked before the response is sent, the persisted configuration SHALL have `enabled = false`, and a restart SHALL be scheduled

#### Scenario: Missing CSRF header

- **WHEN** a client POSTs `/api/display` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected and no configuration SHALL be written

#### Scenario: Out-of-range values are clamped, not rejected

- **WHEN** a client POSTs `/api/display` with `{"enabled": true, "rotation": 9, "interval": 1, "warn_frost_c": 999, "warn_humidity_pct": -5}`
- **THEN** the persisted configuration SHALL contain a `rotation` in 0..3, an `interval` in 10..3600, and warning thresholds that are either in their valid range or disabled

#### Scenario: Enabling or reconfiguring does not drive the panel

- **WHEN** a `POST /api/display` request enables the display or changes only rotation, interval, value fields or warning thresholds
- **THEN** the handler SHALL return without driving the panel; only the disable transition performs a blanking refresh
