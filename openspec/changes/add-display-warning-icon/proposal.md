# Proposal: add-display-warning-icon

## Why

The e-paper panel is the thing people actually look at, but today it only ever
reports *measurements* — it cannot raise a condition that needs attention. A
faulty sensor, an over-temperature safety trip, or a user-defined threshold
crossing (frost risk, high humidity) is visible only in logs, MQTT or the web
UI, none of which anyone glances at from across the room. The panel already has
a disciplined refresh policy, a partial-refresh window large enough to host an
annotation, and a house style of drawn symbols (degree rings, control symbol);
a warning icon fits all three without introducing any new full-refresh
trigger.

Scope decisions from exploration:

- **Layout**: a large drawn warning triangle (~45–50 px) in the left margin
  beside the values, with a short uppercase 5x7 label below it. The values
  never move or shrink — the warning is purely additive. WiFi/MQTT-down
  warnings are explicitly deferred to a later change.
- **Labels**: fixed compile-time vocabulary, not user-supplied free text —
  known to fit the ~55 px margin, no truncation behaviour to spec.
- **Multiple conditions**: single icon slot; most severe wins (priority encoded
  in the condition enum order). No rotation — cycling labels on e-paper would
  pin refreshes at the minimum-interval floor.

## What Changes

- New drawn warning icon (triangle + label) in the value block's left margin,
  inside the existing partial-refresh window, rendered only while a warning is
  active.
- New native-testable `Display::WarningPolicy` sibling to `RefreshPolicy`:
  conditions in, one warning token out, anti-flap dwell state inside. A change
  of token (including none) is a change worth showing to the refresh policy.
- Warning onset bypasses the minimum-interval floor (a late warning is
  dangerous); warning clearance is subject to it (a lingering warning is
  harmless). A minimum dwell time prevents icon flapping around a threshold.
- Configurable user thresholds — frost temperature and humidity limit — added
  to `DisplayConfig`, persisted to NVS (keys ≤ 15 chars, compile-time checked),
  validated in pure C++, exposed through the existing `GET`/`POST /api/display`
  route.
- Warning conditions in this change: sensor snapshot invalid, over-temperature
  safety trip, controller state UNCERTAIN, humidity above threshold, temperature
  below frost threshold. Each condition maps to a fixed label
  (`SENSOR`, `OVERHEAT`, `ACTUATOR`, `HUMID`, `FROST`).

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `display`: new requirements for the warning icon layout, the fixed label
  vocabulary, warning priority, and the anti-flap dwell; the refresh-policy
  requirement gains the warning token as a change worth showing, with onset
  bypassing the interval floor.
- `configuration`: `DisplayConfig` gains the user warning thresholds
  (`warn_frost_c`, `warn_humidity_pct`) with defaults, NVS keys and validation;
  threshold warnings are disabled by default (a threshold of "off").
- `http-api`: the Display endpoints requirement's GET/POST field list gains the
  warning threshold fields, following the same clamp-don't-reject convention.

## Impact

- **Source**: `src/display/` (new `WarningPolicy.{h,cpp}`; `EPaperDisplay`
  gains the drawn icon + label; `DisplayManager` gathers conditions and passes
  the token through), `src/Config.{h,cpp}` (DisplayConfig fields, NVS keys,
  validation), `src/routes/DisplayRoutes.cpp` (new fields in GET/POST).
- **Tests**: native unit tests for `WarningPolicy` (thresholds, priority,
  dwell, onset-bypass) alongside the existing `RefreshPolicy` tests.
- **Web UI**: the settings page's E-Paper Display section gains threshold
  inputs (implementation follows the existing section pattern; no spec change —
  the sections requirement is "at least").
- **Out of scope**: WiFi/MQTT-down warnings (deferred), warning rotation across
  multiple simultaneous conditions, user-supplied free-text labels, takeover
  screens that replace the values, any new full-refresh trigger.
- **Brownout interaction**: warning onset/clearance are partial refreshes
  inside the existing window; the change adds no new full-panel refresh
  triggers, so the open `assess-display-brownout-risk` economics are unchanged
  apart from a marginal increase in partial-refresh frequency.
