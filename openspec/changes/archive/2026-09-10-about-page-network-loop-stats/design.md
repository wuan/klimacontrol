## Context

`/api/about` already emits the four `net_cycle_count` /
`net_avg_cycle_work_ms` / `net_min_cycle_work_ms` /
`net_max_cycle_work_ms` keys as top-level fields in the Network info
section (see change `move-about-network-stats-to-network-section`).
The Device Info page at `GET /about` (HTML in `data/about.html`)
fetches that JSON and renders sections into `<div>` elements, but the
JavaScript in `about.html` only reads WiFi / AP fields into the
`networkInfo` div and Sensor Monitor stats into the `statsInfo` div.
The new `net_*` keys are ignored, so the page does not display them.

The change is a single-file edit to `data/about.html`: extend the
`networkHTML` string assembly so the four `net_*` values appear as
additional rows in the Network section. The PlatformIO pre-build hook
`scripts/compress_web.py` regenerates `src/generated/*_gz.h` from
`data/*.html`, so the firmware build picks up the change with no
manual regeneration step.

## Goals / Non-Goals

**Goals:**

- Render the four `net_*` keys in the Device Info page's Network
  section, in every WiFi state (connected, AP, transitioning).
- Use labels that distinguish the Network-loop stats from the
  Sensor Monitor's cycle stats rendered in the Statistics section.

**Non-Goals:**

- No change to the JSON shape, no change to the API endpoint, no
  change to `src/routes/StatusRoutes.cpp`.
- No change to other pages (`index.html`, `control.html`,
  `config.html`, `settings.html`) — they do not render `/api/about`.
- No change to the gzipped-asset build pipeline beyond what the
  pre-build hook already does.

## Decisions

### D1. Render the rows inside the existing `networkHTML` string

The Device Info page already assembles a `networkHTML` string with
three branches (connected, AP, transition). The cleanest place to
add the four new rows is at the *end* of each branch so they appear
below the WiFi / AP fields but always at the bottom of the Network
section, regardless of state. This keeps the conditional logic
(WiFi vs AP vs transition) untouched and adds the new rows in a
single edit.

Alternative considered: build a fourth always-on block after the
`if / / else if / / else` chain. Rejected because it duplicates the
`networkHTML` write to `document.getElementById('networkInfo')` for
each branch — the chosen approach is fewer lines and matches the
existing structure.

### D2. Labels use "Network Loop" prefix

The Sensor Monitor's cycle stats are rendered as plain "Cycle Count",
"Avg Cycle Delay", "Min Cycle Delay", "Max Cycle Delay" in the
Statistics section. For the Network-loop stats, the same words would
be confusing — both sections would have a "Cycle Count" row pointing
at different counters. Prefixing with "Network Loop" disambiguates
without renaming the underlying JSON fields. Final labels:

- `net_cycle_count` → "Network Loop Cycle Count"
- `net_avg_cycle_work_ms` → "Network Loop Avg Work"
- `net_min_cycle_work_ms` → "Network Loop Min Work"
- `net_max_cycle_work_ms` → "Network Loop Max Work"

The "Work" suffix (matching `_work_ms` in the JSON) distinguishes
from the Sensor Monitor's "Delay" suffix. The "Avg" / "Min" / "Max"
prefixes match the existing Statistics-section convention.

### D3. `net_cycle_count` rendered as bare integer, `*_work_ms` with ` ms` suffix

The JSON values are `uint64_t`. The Statistics section already
formats its integers with no suffix (Cycle Count → `42`) and its
work durations with ` ms` (Avg Cycle Delay → `350 ms`). The new
rows follow the same convention so a reader skimming the page does
not have to context-switch between formatting styles.

## Risks / Trade-offs

- **No graceful fallback if a field is absent.** The current change
  adds the rows unconditionally; if an older firmware (without the
  net_* keys) is queried by a newer page, the rows will read
  `undefined`. The page is bundled into the firmware at build time,
  so the only mismatch is a development / staging case where the
  HTML and the firmware are out of sync — a build artefact problem,
  not a runtime concern. Not addressed.
- **Section heading unchanged.** The Network section's `<h2>Network</h2>`
  header is not renamed (e.g. to "Network & Loop Stats") because the
  WiFi / AP fields still dominate the section's purpose. The new
  rows simply extend what is shown.

## Migration Plan

None. The change is a single edit to `data/about.html`. The
gzipped-asset build pipeline regenerates `src/generated/*_gz.h`
on the next `pio run` invocation. No data migration, no schema
migration, no version bump.

Rollback: revert the single edit. The gzipped-asset regeneration
will pick up the reverted `data/about.html` automatically on the
next build.

## Open Questions

None. The placement (bottom of Network section in every branch),
the labels (Network Loop * Work), and the format (bare integer /
` ms` suffix) all follow established patterns from the existing
Statistics section.