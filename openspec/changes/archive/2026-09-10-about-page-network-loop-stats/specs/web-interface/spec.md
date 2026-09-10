## ADDED Requirements

### Requirement: Device Info page Network section shows loop timing stats

The Device Info page's Network section SHALL render the four Network-loop
work-duration fields emitted by `GET /api/about` alongside the existing
WiFi / AP fields. The four fields SHALL be named `net_cycle_count`,
`net_avg_cycle_work_ms`, `net_min_cycle_work_ms`, and
`net_max_cycle_work_ms` in the JSON; the page SHALL display them with
labels that distinguish them from the Sensor Monitor's cycle-delay
fields shown in the Statistics section (`stats.cycle_count`, etc.).

The `net_cycle_count` value SHALL be formatted as an integer; the three
`*_work_ms` values SHALL be formatted as integers followed by the ` ms`
suffix, matching the convention already used for the Sensor Monitor's
`avg_cycle_delay` / `min_cycle_delay` / `max_cycle_delay` fields in the
Statistics section. The fields SHALL be rendered in every WiFi state
(connected, AP mode, transitioning) — the API emits them
unconditionally, and the page SHALL NOT gate their rendering on
`wifi_ssid` or `ap_ssid` being present.

#### Scenario: Network loop stats render in connected state

- **WHEN** the Device Info page receives a valid `/api/about` response
  containing `net_cycle_count`, `net_avg_cycle_work_ms`,
  `net_min_cycle_work_ms`, and `net_max_cycle_work_ms` while in STA
  mode (so `wifi_ssid` is present)
- **THEN** all four values are visible in the Network section of the
  page, alongside the existing WiFi fields (SSID, Signal, IP Address,
  etc.)

#### Scenario: Network loop stats render in AP mode

- **WHEN** the Device Info page receives a valid `/api/about` response
  while the device is in AP mode (so `ap_ssid` is present instead of
  `wifi_ssid`)
- **THEN** all four `net_*` values are still visible in the Network
  section, alongside the AP fields (SSID, IP Address, Clients)

#### Scenario: Network loop stats render during transition

- **WHEN** the Device Info page receives a valid `/api/about` response
  where neither `wifi_ssid` nor `ap_ssid` is present (the WiFi
  supervisor is between states)
- **THEN** all four `net_*` values are still visible in the Network
  section, alongside the "Disconnected" status row

#### Scenario: Network loop stats are distinguishable from sensor cycle stats

- **WHEN** the Device Info page renders both the Network section and
  the Statistics section
- **THEN** the labels for `net_cycle_count` and the Sensor Monitor's
  `stats.cycle_count` are visibly different (e.g. "Network Loop
  Cycle Count" vs. "Cycle Count") so a reader does not confuse the
  two — the Network section's values describe the Network task's
  per-iteration work duration; the Statistics section's values
  describe the Sensor Monitor task's per-tick sleep duration