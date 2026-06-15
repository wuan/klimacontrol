# system-architecture Specification (delta)

## Purpose

The system-architecture spec already includes a "Memory budget"
requirement that bounds total flash usage and triggers a restart on
catastrophic heap loss. This delta adds a *fragmentation* requirement
on top of that — bounding the *shape* of the free heap, not just its
size — so the firmware can be diagnosed before reaching the
catastrophic-loss restart.

## MODIFIED Requirements

### Requirement: Memory budget

The firmware SHALL keep flash usage under 2 MB (the partition
allocation) and SHALL maintain at least 64 KB of free heap to
allow OTA updates. The system SHALL restart cleanly if free heap
drops below 16 KB during runtime outside of an OTA flow. The
firmware SHALL additionally publish
`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (in bytes) via
the `/api/status` endpoint under the field `largest_free_block` so
that heap *shape* regressions — fragmentation with healthy total
free heap — are observable without serial-log access.

#### Scenario: Build size check

- **WHEN** the build completes
- **THEN** the reported flash usage SHALL be less than 1900 KB (the partition cap) and the build SHALL fail otherwise

#### Scenario: Low heap during runtime

- **WHEN** `ESP.getFreeHeap()` returns less than 16384 bytes and no OTA update is in progress
- **THEN** the firmware SHALL log a CRITICAL line and call `ESP.restart()` within 1 second

#### Scenario: Heap shape is exposed via the status API

- **WHEN** a client GETs `/api/status` while the firmware is
  running normally (free heap > 64 KB)
- **THEN** the response JSON contains a `largest_free_block` key
  whose value is the size in bytes of the largest contiguous free
  block in the 8-bit-capable heap, so an operator can detect
  fragmentation regressions from the web UI
