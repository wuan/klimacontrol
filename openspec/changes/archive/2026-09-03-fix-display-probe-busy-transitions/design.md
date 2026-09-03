## Context

The display spec (`openspec/specs/display/spec.md:640-705`) describes a
`probe()` function that detects whether an e-paper panel is physically
present by driving a manual reset pulse and watching for both BUSY
transitions. The shipped `EPaperDisplay::probe()`
(`src/display/EPaperDisplay.cpp:339-415`) does not implement that
check — it calls `GxEPD2::display.init()` (which the comment at
`EPaperDisplay.cpp:315-326` admits silently succeeds when no panel is
connected) and then waits for BUSY=HIGH, which is true for both
healthy and missing panels, so the function always returns `true`.

`DisplayManager::tryBeginForApInfo()` (`src/display/DisplayManager.cpp:46-67`)
calls `panel.begin(config.rotation)` rather than `panel.probe(...)`,
trusting `begin()` to detect the no-panel case. `begin()` returns
`!faulted`, and `faulted` only flips to `true` after three consecutive
*refresh* timeouts — never on `init()`. So `begin()` returns `true` on
a fresh boot with no panel, `tryBeginForApInfo()` returns `true`,
`Network::startAP()` takes the WPA2-PSK branch, and the user is locked
out because the password is rendered on a panel that does not exist.

## Goals / Non-Goals

**Goals:**

- `EPaperDisplay::probe(timeoutMs)` distinguishes "panel responding"
  from "no panel" by observing both BUSY transitions during a manual
  RST pulse. No SPI, no GxEPD2 state.
- `DisplayManager::tryBeginForApInfo()` calls `probe()` first and only
  proceeds to `panel.begin()` when `probe()` returns true.
- A factory-fresh device with no panel connected enters AP mode as an
  *open* AP. A factory-fresh device with a panel connected still
  enters AP mode as WPA2-PSK with the password rendered on the panel.
- The native test build continues to compile and exercise the boundary;
  a new native test pins the probe-then-begin sequence so a refactor
  cannot silently drop the probe step.
- Comment accuracy: the misleading "we previously tried a hand-rolled
  BUSY probe" rationale in `EPaperDisplay::probe()` and the
  "BUSY-cycle check proved unreliable" paragraph in `begin()` are
  removed/updated.

**Non-Goals:**

- Adding a SPI register read or any other active probe mechanism.
  The spec already names the BUSY-transition check as the presence
  test, and the SSD1681 BUSY line is the panel's contract — a healthy
  panel always drives BUSY LOW during reset processing. There is no
  need to second-guess the existing spec.
- Re-running the probe inside `Network::startAP()` itself. The
  `DisplayManager` boundary is the right place for it: it owns the
  SPI bus and the `initialised` flag, and it already exposes the
  deferred bring-up API the spec describes.
- A retry inside `tryBeginForApInfo` if the first `probe()` fails.
  A single failure falls back to open AP; retrying would mask the
  hardware issue or delay the AP by N × the probe budget.
- Touching `Network::startAP()`. Its decision is already
  `tryBeginForApInfo`-driven, so once the manager is correct, the AP
  mode picks up the correct security level automatically.

## Decisions

### D1 — `probe()` implements the spec literally (manual RST pulse + BUSY transitions)

The spec already lists the four behaviours:

1. Configure CS=HIGH, DC=LOW, RST=HIGH output, BUSY=input.
2. Reset pulse: RST HIGH → LOW → HIGH, ≥10 ms each, then ≥20 ms settle.
3. Watch BUSY: must transition LOW (processing) then HIGH (idle).
4. Return true only if both transitions are observed within
   `timeoutMs`.

The shipped implementation diverges at step 2 — it calls
`display.init()` instead of driving the RST pulse directly, then waits
for BUSY=HIGH at step 3. That divergence is the bug. Restoring the
spec sequence:

```cpp
bool EPaperDisplay::probe(uint32_t timeoutMs) {
    pinMode(DisplayPins::CS, OUTPUT);
    pinMode(DisplayPins::DC, OUTPUT);
    pinMode(DisplayPins::RST, OUTPUT);
    pinMode(DisplayPins::BUSY, INPUT);
    digitalWrite(DisplayPins::CS, HIGH);   // deselect
    digitalWrite(DisplayPins::DC, LOW);     // command mode
    digitalWrite(DisplayPins::RST, HIGH);   // idle

    // Step 2: reset pulse — ≥10 ms in each state, then ≥20 ms settle.
    const uint32_t start = millis();
    delay(10);
    digitalWrite(DisplayPins::RST, LOW);
    delay(10);
    digitalWrite(DisplayPins::RST, HIGH);
    delay(20);

    // Step 3: watch BUSY for both transitions.
    bool sawBusyLow = false;
    while (millis() - start < timeoutMs) {
        if (digitalRead(DisplayPins::BUSY) == LOW) { sawBusyLow = true; break; }
        delay(1);
    }
    if (!sawBusyLow) {
        ESP_LOGW(TAG, "Display probe: BUSY never LOW — no panel");
        return false;
    }
    bool sawBusyHigh = false;
    while (millis() - start < timeoutMs) {
        if (digitalRead(DisplayPins::BUSY) == HIGH) { sawBusyHigh = true; break; }
        delay(1);
    }
    if (!sawBusyHigh) {
        ESP_LOGW(TAG, "Display probe: BUSY stuck LOW — panel stuck");
        return false;
    }
    return true;
}
```

The "previous attempt failed" comment in the existing implementation
refers to a hand-rolled sequence that *also* sent a SPI SWRESET
command and then waited for BUSY=HIGH — the SPI command was the issue,
not the BUSY-transition check. With the SWRESET removed, the
BUSY-transition check is the same logic that `GxEPD2_EPD::_waitWhileBusy`
implements internally: `_busy_level = HIGH` for this panel, so the
controller is "busy" when BUSY=LOW and "idle" when BUSY=HIGH. The
hardware contract matches the software.

### D2 — `tryBeginForApInfo()` calls `probe()` first, `panel.begin()` only on success

The deferred bring-up becomes a two-step sequence:

```cpp
bool DisplayManager::tryBeginForApInfo(const Config::DisplayConfig &config) {
    if (enabled) {
        return true;  // already in normal operation, panel obviously present
    }
    // Spec'd presence check — distinguishes "no panel" from "panel
    // responding" without touching the SPI bus.
    if (!panel.probe(250)) {
        ESP_LOGW(TAG, "No display detected at AP-mode entry");
        return false;
    }
    // Panel responded to the probe; bring it up via the proven
    // GxEPD2 init path so the subsequent showApInfo() can paint.
    if (!panel.begin(config.rotation)) {
        ESP_LOGW(TAG, "Display probe passed but init failed");
        return false;
    }
    return true;
}
```

The 250 ms budget covers a healthy panel (the manual reset cycle
completes in well under 100 ms) with a comfortable margin; on a
truly absent connector the probe exits the LOW-wait loop after
250 ms. `panel.begin()` runs the proven `GxEPD2::display.init()`,
which still silently succeeds with no panel — but at that point the
probe has already rejected the absent-panel case, so `begin()` is
only ever reached when a panel responded.

### D3 — `panel.begin()` comment is updated to admit the new check exists

The existing comment block at `EPaperDisplay.cpp:315-326` reads:

> GxEPD2's `display.init()` does not fail when no panel is connected
> — the SPI writes silently succeed and it returns true. We previously
> added a BUSY-cycle check to verify the panel actually responded. It
> proved unreliable on real panels that finish their init cycle before
> we start polling: BUSY reads HIGH from the start, so the
> LOW-then-HIGH wait times out. Trust `display.init()` here — if the
> panel is missing or broken, the subsequent `show*` / paint calls
> will silently fail.

This describes the *absence* of the check, which is exactly the bug.
The paragraph is replaced with: the presence check is in `probe()`,
called from `tryBeginForApInfo()` before `begin()` runs, and
`begin()` itself only runs when `probe()` already verified the panel
is responding. The "show* / paint calls will silently fail" tail is
kept as the fallback for a panel that passes the probe but later
trips the BUSY fault guard.

### D4 — Native test pins the `probe → begin` sequence

`EPaperDisplay::probe()` keeps its `#ifdef ARDUINO` guard and its
native stub that returns `false`. The native test
`test_display_probe_busy_transitions` adds two regression pins:

- The `DisplayManager::tryBeginForApInfo()` boundary must NOT be
  reachable on native (it is `#ifdef ARDUINO`), but the contract
  the change relies on is captured by a sentinel: a header-side
  comment in `EPaperDisplay.h` asserts the call sequence, and the
  native test for `probe()` itself confirms the stub returns false
  (so any native-build code path that branches on the probe result
  sees "no panel").
- The pre-existing `test_display_manager_ap_info_probe` already
  pins the `DisplayConfig` defaults; that test is left in place and
  gains a new assertion that `tryBeginForApInfo()`'s precondition
  (`DisplayConfig.enabled == false`) is the only thing the cold-boot
  AP probe path relies on.

## Risks / Trade-offs

- **False negative on a healthy panel**: a panel that is wired up but
  whose BUSY line is faulty (stuck HIGH or stuck LOW) trips the probe
  to false. The AP comes up open — the same fallback as today — and
  the user can configure WiFi from a phone. This is the safer
  failure mode (a false positive locks the user out, a false negative
  just falls back to open).
- **False negative on a healthy panel with slow BUSY**: the SSD1681
  is specified to assert BUSY=LOW within 1 ms of reset; the probe
  reads BUSY at 1 ms intervals, so a healthy panel is caught well
  inside the 250 ms budget. A stuck panel trips the timeout. There
  is no documented panel state that "responds to reset" with a
  multi-second BUSY-LOW delay — that would itself be a fault, and
  tripping the probe is the right outcome.
- **Probe runs on every AP-mode entry** (already in the spec). The
  cost is one RST pulse and a few hundred milliseconds of polling on
  a healthy panel, ≤`timeoutMs` on an absent one. AP mode is rare
  (first boot, every third failure), so this is acceptable.
- **RST/CS/DC pin state during the probe**: the probe configures
  these as outputs explicitly, which is the same state `display.init()`
  leaves them in. The subsequent `panel.begin()` reconfigures them
  anyway, so no flag persists between the probe and the begin.
- **Two resets in a row on the AP-info path**: probe resets the
  panel, `panel.begin()` resets it again via `_reset()` inside
  `GxEPD2_EPD::init()`. The SSD1681 tolerates back-to-back resets;
  the panel ends up correctly initialised.

## Migration Plan

No data migration. NVS keys are unchanged. The user-visible change
is the AP security mode on factory-fresh devices with no panel
connected — they now see an *open* AP, where they previously saw
WPA2-PSK with no visible password.

- **Boot into STA mode:** no change. `startAP()` does not run.
- **Cold-boot first-WiFi path (factory-fresh, no panel connected):**
  `probe()` returns false, the AP comes up open, the user joins and
  configures. (Previously: WPA2-PSK with no visible password — user
  was locked out.)
- **Cold-boot first-WiFi path (factory-fresh, panel connected):**
  `probe()` returns true, `panel.begin()` brings the panel up,
  `showApInfo()` paints the password. Unchanged from today.
- **Every-third-failure fallback:** unchanged.

## Open Questions

- *Should `tryBeginForApInfo` set `enabled = true` on success so a
  later `update()` tick repaints the boot splash instead of the AP
  info?* No. `apModeActive` already suppresses `update()` while the
  AP info screen is on the panel, and setting `enabled` here would
  override the user's `DisplayConfig.enabled` preference — the user
  may have deliberately disabled the status display for the next
  STA-mode boot, and we must not flip that.
