# Tasks: AP password via e-paper display detection

Ordered so each step is independently testable. The probe and the decision
are the heart of the change; everything else is rendering.

## 1. Spec scaffolding

- [x] 1.1 `openspec validate 2026-09-04-ap-password-via-display --strict`
      passes against the artifacts in `openspec/changes/2026-09-04-ap-password-via-display/`

## 2. E-paper BUSY pin probe

- [x] 2.1 Add `static bool EPaperDisplay::probe(uint32_t timeoutMs)` to
      `src/display/EPaperDisplay.h` and implement in `EPaperDisplay.cpp`.
      The probe configures CS/DC/RST as outputs and BUSY as input, drives
      `RST` high → low → high (panel reset), then watches BUSY for the
      HIGH-then-LOW transition with the total time capped at `timeoutMs`.
      Returns true only if both transitions are observed.
- [x] 2.2 The probe does NOT call `display.init()` or otherwise touch
      the static GxEPD2 instance. It is a pure pin-level probe.
- [x] 2.3 `#ifdef ARDUINO` guard the implementation. The native build
      gets a stub returning false (display detection is hardware-only).

## 3. AP-info screen on the panel

- [x] 3.1 Add `void EPaperDisplay::showApInfo(const char *ssid, const char *password, const char *ip)`
      to `EPaperDisplay.h` and implement in `EPaperDisplay.cpp`. Uses
      the same fonts as `showSplash` but with a three-line body: SSID,
      password, IP. Calls `feedWatchdog` before/after the paged draw loop
      (the existing blocking-call-safety rule).
- [x] 3.2 `#ifdef ARDUINO` guard.

## 4. DisplayManager API for one-shot AP info

- [x] 4.1 Add `bool tryBeginForApInfo(const Config::DisplayConfig &config)` to
      `DisplayManager.h`. Implementation: if `enabled` is already true
      (panel is in normal operation), return true without touching
      anything. Otherwise probe via `panel.probe(250)`; if the probe
      succeeds, call `panel.begin(config.rotation)`; return whether the
      panel is now initialised.
- [x] 4.2 Add `void showApInfo(const char *ssid, const char *password, const char *ip)`.
      Forwards to `panel.showApInfo(...)` only if the panel is
      initialised. Sets an `apModeActive` flag so the normal `update()`
      tick does not repaint temperature on top of the password screen.
- [x] 4.3 Add `void endApInfo()`. If the panel was inited by us (i.e.
      `enabled == false`), hibernate it. If it was already enabled, do
      nothing — STA mode's normal updates will take over after the user
      submits WiFi credentials and the device restarts.
- [x] 4.4 `update()` checks `apModeActive` and returns early while it
      is set. (Comment cites this change in the existing mutex-block.)

## 5. Network API: pre-configured AP password

- [x] 5.1 Add `void Network::setApPassword(const char *password)` to
      `Network.h`. Stores the password in a member buffer (size 9). An
      empty string means "open AP" and the buffer starts empty by
      default.
- [x] 5.2 Add `bool Network::getApPassword(char *out, size_t outSize) const`
      that copies the stored password to `out` and returns whether the
      password is non-empty. Returns false (and writes nothing) when the
      buffer is empty or `outSize < 9`.
- [x] 5.3 Modify `Network::startAP()` to use the pre-configured
      password: if `getApPassword` returns true, call
      `WiFi.softAP(ssid, password)`; otherwise `WiFi.softAP(ssid)`.
      Keep the `ESP_LOGI` line that announces the SSID.

## 6. Boot wiring in main.cpp

- [x] 6.1 In `setup()`, after `config.begin()` and before
      `network.startTask()`, probe the e-paper panel via
      `Display::EPaperDisplay::probe(250)`.
- [x] 6.2 If the probe succeeds, compute the AP password via
      `Support::computeApPassword` and call `network.setApPassword(...)`.
      If the probe fails, call `network.setApPassword("")` so the AP
      comes up open.
- [x] 6.3 If `DisplayConfig.enabled` is true, the existing `setupDisplay`
      continues to bring the panel up for normal operation (no change).
      If `DisplayConfig.enabled` is false but the probe succeeded, do
      NOT init the panel here — `Network::startAP` does that on demand
      via `tryBeginForApInfo`.
- [x] 6.4 Log the decision at INFO: "AP password strategy: WPA2-PSK (display
      detected)" or "AP password strategy: open (no display detected)".

## 7. Network renders AP info on the panel

- [x] 7.1 In `Network::startAP()`, after deciding WPA2-PSK vs open,
      render the AP info on the panel via `display->tryBeginForApInfo`
      + `display->showApInfo` + `display->endApInfo`. Only do this when
      the password will be used; an open AP has nothing useful to show
      on a panel.
- [x] 7.2 Order matters: the panel rendering happens after
      `WiFi.softAP` so the panel can show the assigned AP IP (which is
      always `192.168.4.1` for the ESP32 SoftAP but using the runtime
      value is more honest).

## 8. Native tests for the pure-C++ parts

- [x] 8.1 The probe itself is `#ifdef ARDUINO` and cannot run on native.
      Document this in the test header.
- [x] 8.2 Test `Network::setApPassword` / `getApPassword`:
      - Default state (no setApPassword call): getApPassword returns false.
      - After setApPassword("12345678"): getApPassword returns true,
        output matches.
      - After setApPassword(""): getApPassword returns false (empty is
        treated as "open AP").
      - Output buffer too small (< 9): getApPassword returns false.
      - Null output buffer: getApPassword returns false without crash.
- [x] 8.3 Test the threat-model inversion: the probe NOT being
      testable on native is itself a property the spec calls out.
      Smoke-test that `Display::EPaperDisplay::probe(...)` returns false
      in the native stub.

## 9. Verify and close out

- [x] 9.1 `pio test -e native` — all green, including the new
      `Network::setApPassword/getApPassword` cases.
- [x] 9.2 `pio run -e adafruit_qtpy_esp32s2` — builds and reports
      the same flash / RAM shape as the previous version (no new
      allocations on the hot path).
- [x] 9.3 Re-read `Network::startAP()`, `main.cpp setup()`, and
      `DisplayManager::tryBeginForApInfo` end-to-end and confirm that:
      - The probe runs exactly once per boot.
      - The decision is held in Network for the rest of the boot.
      - A false-negative probe (BUSY stuck LOW) leaves the AP open, not
        locked.
      - A false-positive probe (BUSY stuck HIGH) leaves the AP open,
        not locked.
      - A real panel response renders the AP info on screen.
- [x] 9.4 Update `docs/CODE_REVIEW.md` finding #19 to note the
      follow-up (the previous fix was incomplete because it had no
      self-contained discovery path; this change closes it).
- [x] 9.5 `/opsx:archive`
