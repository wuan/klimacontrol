# Tasks: defer the display probe to AP-mode entry

Ordered so each step is independently testable. The probe at AP-mode
entry is the heart of the change — sections 1-4 are mechanical cleanup
around it.

## 1. Spec scaffolding

- [x] 1.1 `openspec validate 2026-09-04-probe-deferred-to-ap-mode --strict`
      passes against the artifacts in this directory

## 2. Remove the boot-time probe

- [x] 2.1 In `src/main.cpp::setup()`, remove the entire block that
      probes the panel and calls `network.setApPassword(...)`. The
      block lives between `setupDisplay(...)` and `network.startTask()`.
- [x] 2.2 Remove the includes that are now unused in `main.cpp`:
      `support/ApPassword.h`, `DeviceId.h`, `display/EPaperDisplay.h`.
      `support/LocalTime.h` and `display/DisplayManager.h` stay.

## 3. Move the probe into `Network::startAP()`

- [x] 3.1 In `src/Network.cpp::startAP()`, add a probe block before
      `WiFi.softAP(...)`. The block calls `display->tryBeginForApInfo()`,
      computes the password via `Support::computeApPassword` on
      success, and brings the AP up with WPA2-PSK; on failure the
      AP comes up open.
- [x] 3.2 The AP-info paint on the panel (`display->showApInfo`) and
      the `endApInfo()` follow `WiFi.softAP(...)`, so the panel shows
      the runtime AP IP rather than a hard-coded value.
- [x] 3.3 Remove the now-unused `Network::setApPassword(...)` /
      `getApPassword(...)` methods and the `apPassword` member.

## 4. Read-only AP state for `/api/ap-info`

- [x] 4.1 Add `bool apIsOpen` private member and `bool isApOpen() const`
      public method to `Network`.
- [x] 4.2 In `Network::startAP()`, set `apIsOpen = false` for the
      WPA2-PSK branch and `apIsOpen = true` for the open branch.
- [x] 4.3 In `src/WebServerManager.cpp`, update the `/api/ap-info`
      handler to use `network.isApOpen()` and compute the password
      via `Support::computeApPassword` when `!isApOpen()`. The shape
      of the JSON response (`ssid`, `password`, `open`) is unchanged
      so the captive portal page does not need to change.

## 5. Simplify `tryBeginForApInfo()`

- [x] 5.1 In `src/display/DisplayManager.cpp`, drop the manual
      `panel.probe(250)` step. Call `panel.begin(...)` directly.
- [x] 5.2 Comment the rationale: `panel.begin()` is the proven init
      path that `EPaperDisplay::begin()` uses in normal operation,
      so it is the right thing to delegate to here too.

## 6. Remove unused slot helpers and their tests

- [x] 6.1 Remove `Support::setApPasswordSlot` /
      `getApPasswordSlot` from `src/support/ApPassword.h`. The slot
      pattern no longer has a caller now that `Network::startAP()`
      computes the password on demand.
- [x] 6.2 Remove the corresponding test cases (`test_slot_*`) from
      `test/test_ap_password/test_ap_password.cpp`. The
      `Support::computeApPassword` tests stay.
- [x] 6.3 `pio test -e native` should report 547 cases (was 559).

## 7. Verify and close out

- [x] 7.1 `pio test -e native` — all green, 547 cases
- [x] 7.2 `pio run -e adafruit_qtpy_esp32s2` — builds clean, no
      flash / RAM shape change from the baseline
- [x] 7.3 Re-read `Network::startAP()` and `DisplayManager::tryBeginForApInfo()`
      end-to-end. Confirm that:
      - The probe runs only inside `startAP()`.
      - `main.cpp` no longer reads the panel.
      - The captive portal page is fed the correct shape on both
        branches (`open: true` vs `password + open: false`).
- [x] 7.4 Update `docs/CODE_REVIEW.md` finding #19 to note the
      follow-up (probe moved to AP-mode entry; the bug with the
      hand-rolled BUSY probe is now fixed by delegating to
      `panel.begin()`).
