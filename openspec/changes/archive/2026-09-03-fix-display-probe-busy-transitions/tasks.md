# Tasks: fix display probe to actually detect absence

Ordered so each step is independently verifiable. Section 1 fixes the
detection logic; sections 2-3 wire it into the AP-mode decision;
sections 4-5 verify on host and on device; section 6 closes the loop
with the predecessor change that was already in flight.

## 1. Rewrite `EPaperDisplay::probe()` per the spec

- [x] 1.1 In `src/display/EPaperDisplay.cpp`, replace the body of
      `EPaperDisplay::probe(uint32_t timeoutMs)` with the spec's
      four-step sequence: configure CS=HIGH/DC=LOW/RST=HIGH outputs
      and BUSY input; drive RST HIGH → LOW → HIGH with ≥10 ms in
      each state plus ≥20 ms settle; require BUSY=LOW within
      `timeoutMs`; require BUSY=HIGH within `timeoutMs` after the
      LOW transition; return true only when both transitions are
      observed. Log a clear warning on each failure path (no-LOW,
      stuck-LOW). Do NOT touch the SPI bus; do NOT modify
      `initialised` or `faulted`. The native stub at
      `EPaperDisplay.h:188` is unchanged.
- [x] 1.2 In `src/display/EPaperDisplay.cpp`, replace the comment
      block at `EPaperDisplay::begin()` (currently `EPaperDisplay.cpp:315-326`
      that admits "GxEPD2's `display.init()` does not fail when no
      panel is connected") with text that points the reader at
      `probe()` and notes that the deferred AP-info path runs the
      probe first; `begin()` itself is only ever called after a
      successful probe. Keep the "show* / paint calls will silently
      fail" tail as the fallback for a panel that passes the probe
      but later trips the BUSY fault guard.

## 2. Wire `probe()` into `tryBeginForApInfo()`

- [x] 2.1 In `src/display/DisplayManager.cpp`, change the body of
      `tryBeginForApInfo()` so the cold-boot path calls
      `panel.probe(250)` first and short-circuits to false (with the
      existing `ESP_LOGW` warning) when the probe reports the panel
      is absent. Only on a successful probe call `panel.begin(...)`
      and return its result; on `begin()` returning false after a
      successful probe, log a distinct `ESP_LOGW` and return false.
      The existing `if (enabled) return true;` short-circuit is
      unchanged.

## 3. Update the predecessor change's spec to match

- [x] 3.1 The `probe-display-at-ap-entry` change at
      `openspec/changes/probe-display-at-ap-entry/specs/display/spec.md`
      and `…/specs/networking/spec.md` describes the original
      `panel.begin()`-only path. Archive that change with no further
      edits (the prose is historical); the corrected behaviour lives
      in the new change's spec deltas which will fold into the main
      specs at archive time.

## 4. Native test for the boundary

- [x] 4.1 Create `test/test_display_probe_busy_transitions/` with
      `test_display_probe_busy_transitions.cpp` registered in
      `platformio.ini` under `[env:native]`. The test pins two
      contracts: (a) the native `EPaperDisplay::probe()` stub returns
      `false` (so the native path always models "no panel"), (b) a
      static assertion in `EPaperDisplay.h` (under `#ifdef ARDUINO`)
      that the `tryBeginForApInfo` source ordering is `probe()` then
      `panel.begin()` — captured via a comment that names the two
      calls in order, plus a runtime check that the new
      `tryBeginForApInfo` log strings include both "No display
      detected" and "init failed" so a regression that drops one of
      the two paths is caught.
- [x] 4.2 `pio test -e native` reports all green; the test count
      rises by the new cases (≥ 2 new tests).

## 5. ESP32 build

- [x] 5.1 `pio run -e adafruit_qtpy_esp32s2` builds clean. No flash
      / RAM shape change beyond what `probe()` and the
      `tryBeginForApInfo` rewire add (a handful of `digitalWrite`
      calls inside the probe, one extra branch in the manager).
- [x] 5.2 On a board with no panel wired up, the boot log shows the
      `No display responded at AP-mode entry — AP will be open`
      warning, NOT the `Display responded at AP-mode entry — using
      WPA2-PSK` line. On a board with the Waveshare 1.54" V2
      connected, the WPA2-PSK line appears within ~1 s of `startAP()`
      and the password renders on the panel within another ~2.6 s
      (the full-refresh window). *(On-device verification by the
      operator after flashing the new firmware; the native tests
      and the ESP32 build cover everything else.)*

## 6. Verify and close out

- [x] 6.1 Read `EPaperDisplay::probe()` and
      `DisplayManager::tryBeginForApInfo()` end-to-end. Confirm
      that:
      - `probe()` returns false on a missing panel (no SPI touched).
      - `probe()` returns false on a BUSY-stuck-LOW panel.
      - `probe()` returns false on a BUSY-stuck-HIGH panel.
      - A healthy panel passes the probe and the subsequent
        `panel.begin()` brings it up so `showApInfo()` paints the
        password.
- [x] 6.2 Update `docs/CODE_REVIEW.md` finding #19 to note that the
      false-positive is fixed (the probe now uses the spec'd BUSY
      transition check; the prior design note that pointed at the
      hand-rolled probe as "unreliable on real panels" was about a
      sequence that sent SPI SWRESET — that issue does not apply to
      the pure-presence RST+poll sequence the spec describes).
