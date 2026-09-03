# Tasks: probe the display at AP-mode entry

Ordered so each step is independently testable. Sections 1-3 ship the
deferred probe the spec already describes; sections 4-6 verify the
behaviour on device and on host.

## 1. Spec scaffolding

- [x] 1.1 `openspec validate probe-display-at-ap-entry --strict` passes
      against the artifacts in this directory

## 2. Add `DisplayManager::tryBeginForApInfo`

- [x] 2.1 In `src/display/DisplayManager.h`, declare
      `bool tryBeginForApInfo(const Config::DisplayConfig &config);`
      with a doc comment that names the two return paths (manager
      already enabled → true; otherwise → `panel.begin()` and return
      its result)
- [x] 2.2 In `src/display/DisplayManager.cpp`, define
      `tryBeginForApInfo`. Short-circuit on `enabled`, log a warning
      on the `panel.begin()` failure path, return the boolean. The
      rationale ("`panel.begin()` is the proven init path that
      `EPaperDisplay::begin()` uses in normal operation, so it is the
      right thing to delegate to here too") goes in a comment

## 3. Wire the probe into `Network::startAP()`

- [x] 3.1 In `src/Network.cpp::startAP()`, replace the
      `display->isEnabled()` branch with `display->tryBeginForApInfo(...)`.
      On true, compute the password via `Support::computeApPassword`
      and bring the AP up WPA2-PSK; on false, fall through to the
      existing open-AP branch
- [x] 3.2 Update the log lines. The two today's placeholders
      (`Display disabled in config — AP will be open ...` and
      `open — no display detected`) collapse into one
      `No display responded at AP-mode entry — AP will be open`. The
      success-path line keeps the `WPA2-PSK` wording so the boot log
      still distinguishes the two outcomes
- [x] 3.3 Leave `display->showApInfo(...)` + `display->endApInfo()`
      calls on the WPA2-PSK branch untouched — they are the right
      thing to paint the password on the panel after `WiFi.softAP()`
      has set up the runtime AP IP

## 4. Native test for the new boundary

- [x] 4.1 Create `test/test_display_manager_ap_info_probe/` with
      `test_display_manager_ap_info_probe.cpp` registered in
      `platformio.ini` under `[env:native]`. The test exercises the
      native-build contract: `DisplayManager` is `#ifdef ARDUINO`, so
      the native path models "panel absent" by checking that the
      factory-default code path returns the right result for the
      boundary that *is* visible in native (`Support::computeApPassword`
      produces the expected 8-hex output for a known device id —
      already covered by `test_ap_password`)
- [x] 4.2 `pio test -e native` reports all green; test count goes
      up by the new cases (≥ 1 new test)

## 5. ESP32 build

- [x] 5.1 `pio run -e adafruit_qtpy_esp32s2` builds clean with no
      flash / RAM shape change beyond what `tryBeginForApInfo` adds
      (one function, one bool parameter, no heap)

## 6. Verify and close out

- [x] 6.1 Read `Network::startAP()` and `DisplayManager::tryBeginForApInfo()`
      end-to-end. Confirm that:
      - The probe runs only inside `startAP()`.
      - A factory-fresh device with `DisplayConfig.enabled == false`
        and a connected panel reaches the WPA2-PSK branch.
      - A factory-fresh device with no panel reaches the open-AP branch
        via the probe (not via the config flag).
- [x] 6.2 Update `docs/CODE_REVIEW.md` finding #19 to note that the
      deferred probe is now actually wired up (the spec text already
      promised it; the implementation landed in this change)