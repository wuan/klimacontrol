# web-interface Specification (delta)

## Purpose

The web-interface spec currently documents the dashboard and the
settings page but not the Device Info page (`/about`). This delta
introduces the Device Info page as a documented web-UI surface and
pins its memory-section contents — including the
`largest_free_block` display — so the field has a stable home and
the main dashboard is not encumbered with diagnostic fields.

## ADDED Requirements

### Requirement: Device Info page exists

The firmware SHALL serve a Device Info page at `GET /about`. The
page SHALL be reachable from the main dashboard via a "Device Info"
link in the navigation footer, and SHALL be served as embedded
HTML (not from a filesystem) consistent with the existing pages.

#### Scenario: Loading the Device Info page

- **WHEN** a browser requests `/about` on a device in STA mode
- **THEN** the response SHALL be an HTML page that calls
  `GET /api/about` to populate its fields

### Requirement: Device Info page memory section

The Device Info page SHALL include a "Memory" section that lists,
at minimum, `free_heap`, `min_free_heap`, `heap_size`, and
`largest_free_block`. Each value SHALL be formatted as a
human-readable size in bytes/KB/MB (e.g. `81.5 KB`). The
`largest_free_block` value SHALL be sourced from the
`largest_free_block` field of `GET /api/about`.

#### Scenario: All memory fields render

- **WHEN** the Device Info page receives a valid `/api/about`
  response containing `free_heap`, `min_free_heap`, `heap_size`,
  and `largest_free_block`
- **THEN** all four fields SHALL be visible in the Memory section
  of the page

### Requirement: Main dashboard does not render diagnostic heap fields

The main dashboard at `GET /` SHALL NOT render `largest_free_block`
(or any other heap-fragmentation diagnostic) inline. Heap
diagnostics belong on the Device Info page; the main dashboard
SHALL stay focused on at-a-glance environmental readings
(temperature, relative humidity, dew point, target, control state).

#### Scenario: Dashboard payload sources

- **WHEN** the main dashboard is loaded in a browser
- **THEN** the page does not contain a DOM element bound to
  `largest_free_block` and its JavaScript does not read the
  `largest_free_block` key from `/api/status`
