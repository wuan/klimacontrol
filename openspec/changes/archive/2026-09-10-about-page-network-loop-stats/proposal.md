## Why

Change `move-about-network-stats-to-network-section` moved the four
`net_cycle_count` / `net_avg_cycle_work_ms` / `net_min_cycle_work_ms`
/ `net_max_cycle_work_ms` keys from the `stats` sub-object of
`/api/about` to top-level keys in the Network info section of the
JSON. The firmware now emits them, but `data/about.html` — the
Device Info page that consumes `/api/about` — has not been updated.
The Network section currently renders only the WiFi / AP fields
(`SSID`, `Signal`, `IP Address`, `MAC Address`, etc.); it does not
read the new `net_*` keys, so an operator who lands on `/about` to
inspect network health sees WiFi state but not the Network-loop
timing stats the JSON now exposes.

## What Changes

- The Device Info page's Network section SHALL render the four
  `net_*` keys from `/api/about` alongside the existing WiFi / AP
  fields. Each value SHALL be formatted as an integer (count) or
  integer-with-` ms` suffix (work durations) to match the convention
  already used for the Sensor Monitor's `cycle_count` /
  `avg_cycle_delay` / etc. in the Statistics section.
- The keys are emitted by the API in every WiFi state, so the page
  SHALL render them unconditionally (whether connected, in AP mode,
  or transitioning) — matching the spec for the API endpoint.

**No breaking change to the API**: the JSON shape is unchanged from
`move-about-network-stats-to-network-section`. The change is purely
client-side.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `web-interface`: add a requirement that the Device Info page's
  Network section renders the four `net_*` keys from `/api/about`.

## Impact

- `data/about.html` — append four `createRow(...)` calls inside the
  `networkHTML` string assembly so the rows appear at the bottom of
  the Network section in every WiFi state (connected, AP, transition).
- `src/generated/*_gz.h` — regenerated automatically by
  `scripts/compress_web.py` (the PlatformIO pre-build hook) on the
  next firmware build; no manual step.
- No native-test impact (the HTML is `ARDUINO`-only). No firmware
  build impact beyond the gzipped-asset regeneration.