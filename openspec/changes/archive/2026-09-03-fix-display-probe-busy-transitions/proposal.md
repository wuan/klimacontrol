## Why

The deferred display probe landed in change `probe-display-at-ap-entry`
and was supposed to run inside `Network::startAP()` so the configuration
AP comes up WPA2-PSK when a panel is physically connected — even on a
factory-fresh device with `DisplayConfig.enabled == false`. The
implementation delegates to `panel.begin()`, but `panel.begin()` calls
`GxEPD2::display.init()` which silently succeeds when no panel is
connected (the comment at `EPaperDisplay.cpp:315-326` admits this) and
then returns `true` because the fault guard only trips on three
consecutive refresh timeouts, never on init. A device with nothing on
the SPI connector therefore still detects a panel, the AP comes up
WPA2-PSK, the user is shown a password on a panel that is not there,
and the device is locked out of the only configuration path that works
on a board with no serial cable, no case label, and no panel.

The display spec already describes the right presence check:
`EPaperDisplay::probe()` SHALL drive a manual reset pulse and require
both BUSY transitions (`openspec/specs/display/spec.md:640-705`). The
shipped `probe()` instead calls `display.init()` and waits for BUSY=HIGH,
which is true for both a healthy and a missing panel — so it always
returns `true` regardless of whether anything is wired up.

## What Changes

- **Rewrite `EPaperDisplay::probe()` per the existing spec**: drive the
  manual RST HIGH → LOW → HIGH reset pulse (≥10 ms each, then ≥20 ms
  settle) and require both BUSY transitions (LOW then HIGH) within
  `timeoutMs`. No SPI bus, no `GxEPD2` state, no `initialised` /
  `faulted` mutation. Missing panel, BUSY stuck LOW, and BUSY stuck
  HIGH all return false — the three scenarios the spec already lists.
- **Update `DisplayManager::tryBeginForApInfo()`** to call `probe()`
  first and only call `panel.begin()` when the probe returns true. A
  false probe short-circuits to false without touching the SPI bus or
  the GxEPD2 driver state. A true probe brings the panel up via the
  proven `panel.begin()` path so the subsequent `showApInfo()` can
  actually paint the password.
- **Update the existing comments** that justify the broken design
  (`EPaperDisplay::probe()`'s "delegates to display.init()" comment, the
  `EPaperDisplay::begin()` "BUSY-cycle check proved unreliable" paragraph
  that admits the bug exists). They become accurate again.
- **Add a native test** that exercises the `probe()` stub in the
  `#ifdef ARDUINO` boundary, plus a regression pin on the
  `tryBeginForApInfo` call sequence so a future refactor cannot re-introduce
  the "always returns true" behaviour.

## Capabilities

### Modified Capabilities

- `display`: the `probe()` requirement already mandates the manual RST +
  BUSY-transition check; this change actually implements it. Add a
  Scenario for "panel absent returns false via the BUSY transition
  check" so the spec scenario set explicitly covers the false-positive
  case the bug exposed. The `tryBeginForApInfo` requirement changes
  from "delegates to `panel.begin()`" to "calls `probe()` first, then
  `panel.begin()` only on success".
- `networking`: the "Configuration AP runs WPA2-PSK, not open"
  requirement references `tryBeginForApInfo` → `panel.begin()`. Update
  it to describe the two-step path (`probe()` then `panel.begin()`)
  and the rationale (the spec'd probe catches the no-panel case that
  `panel.begin()` alone misses).

## Impact

- **Source:** `src/display/EPaperDisplay.{h,cpp}` (rewrite `probe()`),
  `src/display/DisplayManager.cpp` (re-wire `tryBeginForApInfo`),
  `src/Network.cpp` (no code change — log line wording in the
  `startAP()` success path still names "Display responded at AP-mode
  entry").
- **Tests:** new native test `test/test_display_probe_busy_transitions`
  covering (a) the `EPaperDisplay::probe()` native stub still returns
  false, (b) a regression pin on the
  `tryBeginForApInfo` call sequence (probe → begin).
- **Spec:** deltas in
  `openspec/changes/fix-display-probe-busy-transitions/specs/display/spec.md`
  and `…/specs/networking/spec.md`.
- **No NVS schema change**, no firmware version bump.
- **Boot behaviour:** unaffected for STA-mode boots (the probe still
  only runs inside `Network::startAP()`). For AP-mode entries the probe
  now actually distinguishes "panel present" from "no panel", so the
  WPA2-PSK vs open-AP decision stops defaulting to WPA2-PSK on hardware
  with no panel wired up.
- **Risk:** `probe()` uses the RST pin directly — no SPI — so a hardware
  fault on the SPI pins cannot affect it. The probe touches BUSY as
  input only, RST/CS/DC as outputs, all of which the GxEPD2 init path
  reconfigures anyway on the subsequent `panel.begin()` call. The
  watchdog is fed inside `panel.begin()` per the existing
  `system-architecture` blocking-call rule; the probe itself completes
  in well under one watchdog period (≤150 ms wall time on a healthy
  panel, ≤`timeoutMs` on a stuck panel).
