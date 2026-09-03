## Why

The previous change (`2026-09-04-probe-deferred-to-ap-mode`) moved
the display probe from boot to AP-mode entry, but missed an
interaction with `setupDisplay()`: the wiring
`network.setDisplay(&displayManager)` was only happening when
`DisplayConfig.enabled == true` *and* `displayManager.begin()`
succeeded. In the common case of a device that boots into AP mode
without WiFi configuration (and has the default
`DisplayConfig.enabled == false`), the DisplayManager pointer was
never installed into Network, so the probe at AP-mode entry fell
through to "no DisplayManager wired — AP will be open".

The captive portal page on a real device showed exactly this: the
AP came up open even though a panel was physically connected.

## What Changes

- `setupDisplay()` always calls `network.setDisplay(&displayManager)`,
  even when `DisplayConfig.enabled == false`. The Network task's
  deferred probe is now reachable in every configuration, not just
  the enabled-display case.
- The actual panel init (`displayManager.begin()`) still only runs
  when `DisplayConfig.enabled == true` — that is the user-visible
  "is the normal status display on?" flag, and we keep its semantics.
  When the flag is false, the panel is brought up on demand at
  AP-mode entry by `Network::startAP()` → `tryBeginForApInfo()`.
- No production code path changes besides the unconditional wire.

### Non-goals

- Auto-enabling the normal status display. The user can still
  toggle `DisplayConfig.enabled` from the web UI; we just stop
  gating the AP-info probe on it.
- Refactoring `setupDisplay()` further. The change is a one-line
  wire move plus a clarifying log.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `networking`: clarify that `Network::startAP()`'s probe is the
  single source of truth for the AP password decision, and that
  the DisplayManager pointer is wired unconditionally at boot so
  the probe can reach the panel even when the normal status
  display is disabled.

## Impact

- **Source:** `src/main.cpp` (`setupDisplay()` always wires the
  DisplayManager pointer).
- **Tests:** no new tests; the existing 547 native tests cover
  the unchanged behaviour.
- **Spec:** delta in
  `openspec/changes/<this>/specs/networking/spec.md`.
- **No NVS schema change**, no firmware version bump.
- **Boot behaviour:** unchanged. The probe still only runs in AP
  mode. The change is just that `network.display` is non-null on
  more boots, so the probe has somewhere to land.
