## ADDED Requirements

### Requirement: Energy settings endpoints

The firmware SHALL expose `GET /api/settings/energy` returning a JSON object with `wifi_power`, `wifi_sleep_mode`, and `led_dark_after_s`, and `POST /api/settings/energy` accepting any subset of those keys. The POST handler SHALL require the `X-Requested-With: KlimaControl` header, SHALL validate `wifi_power` against `{8, 34, 52, 68, 80}`, `wifi_sleep_mode` against `0..2`, and `led_dark_after_s` against `0..3600`, rejecting out-of-range values with HTTP 400 and a JSON error.

After saving, the handler SHALL schedule a restart via `requestRestart(1000)` only if `wifi_power` or `wifi_sleep_mode` differs from the previously stored value. If neither WiFi field changed, the handler SHALL apply `led_dark_after_s` live to the status LED and SHALL NOT restart. The success response SHALL include `"restart": true` or `"restart": false` accordingly.

#### Scenario: GET returns the LED threshold

- **WHEN** a client GETs `/api/settings/energy`
- **THEN** the response SHALL contain `led_dark_after_s` with the persisted value (default `300`)

#### Scenario: Changing only the LED threshold does not restart

- **WHEN** a client POSTs `{"led_dark_after_s": 0}` with the CSRF header and the WiFi fields are unchanged
- **THEN** the value SHALL be persisted, the status LED SHALL stop rendering dark on its next update, the response SHALL be HTTP 200 with `"restart": false`, and no restart SHALL be scheduled

#### Scenario: Changing a WiFi field restarts

- **WHEN** a client POSTs `{"wifi_power": 34}` and the stored `wifi_power` was `52`
- **THEN** the value SHALL be persisted, a restart SHALL be scheduled, and the response SHALL be HTTP 200 with `"restart": true`

#### Scenario: Re-saving unchanged WiFi values does not restart

- **WHEN** a client POSTs the same `wifi_power` and `wifi_sleep_mode` that are already stored
- **THEN** no restart SHALL be scheduled and the response SHALL include `"restart": false`

#### Scenario: Out-of-range LED threshold is rejected

- **WHEN** a client POSTs `{"led_dark_after_s": 7200}`
- **THEN** the response SHALL be HTTP 400 with a JSON error and nothing SHALL be persisted

#### Scenario: Missing CSRF header is rejected

- **WHEN** a client POSTs to `/api/settings/energy` without the `X-Requested-With: KlimaControl` header
- **THEN** the request SHALL be rejected by `verifyCsrfHeader()` and nothing SHALL be persisted
