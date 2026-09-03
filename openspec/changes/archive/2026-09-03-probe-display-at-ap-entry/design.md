## Context

The display spec describes a deferred probe: `Network::startAP()` asks
`DisplayManager::tryBeginForApInfo()` whether a panel is physically
connected, and the answer drives the WPA2-PSK-vs-open decision. The
captive portal then renders SSID + password either on the panel (when one
responded) or as an open-network note (when nothing responded). The
archived change `2026-09-04-probe-deferred-to-ap-mode` designed the path
end-to-end (proposal, design, spec deltas, tasks), the changes were
marked complete, and the spec text still carries the requirement.

None of the code actually shipped. Concretely:

- `DisplayManager::tryBeginForApInfo()` is not declared in
  `DisplayManager.h` and not defined in `DisplayManager.cpp`.
- `Network::startAP()` keys off `display->isEnabled()` instead — the
  placeholder that took its place. A factory-fresh device
  (`DisplayConfig.enabled == false`) logs
  `Display disabled in config — AP will be open` and brings the AP up
  open even when a panel is sitting on the SPI connector. This is the
  user-visible bug.
- `Network::isApOpen()` and `/api/ap-info` are not in the code either.
  The captive portal page renders the WiFi form regardless. For this
  change, the page is left alone — the user-facing API for joining the
  AP is the WPA2-PSK passphrase shown on the panel when one responds.

The `DisplayManager` is already constructed unconditionally in `main.cpp`
and `network.setDisplay(&displayManager)` is wired in before
`network.startTask()`, so the deferred probe can be invoked from
`Network::startAP()` today. The wiring is in place; only the function
and the call are missing.

## Goals / Non-Goals

**Goals:**

- `DisplayManager::tryBeginForApInfo(config)` exists, returns true when a
  panel responds to bring-up, and returns false otherwise.
- `Network::startAP()` calls `tryBeginForApInfo` and uses the result to
  choose between WPA2-PSK (with `showApInfo` + `endApInfo`) and open AP.
- A native test exercises the `tryBeginForApInfo` boundary without the
  panel hardware.
- Spec deltas under `networking` and `display` clarify the deferred
  decision and the new factory-fresh-with-panel scenario.

**Non-Goals:**

- A new `/api/ap-info` endpoint. The user explicitly excluded this from
  scope; the captive portal page is left as-is.
- Updating `data/config.html` to render the password or an "(open
  network)" note. The page still renders the WiFi form; the only
  user-visible change is that the AP now sometimes runs WPA2-PSK and the
  user reads the password off the panel.
- Re-probing at every boot. The probe runs only inside `startAP()` —
  STA-mode boots (the common case) still pay zero probe cost.
- A retry inside `tryBeginForApInfo` if the first attempt fails. A single
  failure falls back to open AP; retrying would mask the underlying
  hardware issue or delay the AP by N× the probe budget.

## Decisions

### D1 — `tryBeginForApInfo` delegates to `panel.begin()`, not the hand-rolled BUSY probe

The hand-rolled `EPaperDisplay::probe()` in
`EPaperDisplay.cpp:339-415` issues a manual reset pulse on RST and
polls BUSY for transitions. The Waveshare 1.54" V2 SSD1681 panel
consistently failed to release BUSY within the budget on this sequence
even when responsive — see the comment block at
`EPaperDisplay.cpp:340-356` and the archived change's design notes.

The proven path is `panel.begin()`, which calls
`GxEPD2::display.init()` — the same init sequence the firmware uses in
normal operation. A healthy panel returns true within ~1 s; a stuck
panel trips the BUSY fault guard after 3 consecutive timeouts and
returns false. So:

```cpp
bool DisplayManager::tryBeginForApInfo(const Config::DisplayConfig &config) {
    if (enabled) return true;             // already in normal operation
    if (!panel.begin(config.rotation)) {  // proven init path
        ESP_LOGW(TAG, "No display detected at AP-mode entry");
        return false;
    }
    return true;
}
```

### D2 — `startAP()` calls `tryBeginForApInfo` instead of checking `isEnabled()`

`Network::startAP()` today has the placeholder branch:

```cpp
if (display->isEnabled()) {
    Support::computeApPassword(deviceId.c_str(), password, sizeof(password));
    useWpa2 = true;
} else {
    ESP_LOGW(TAG, "Display disabled in config — AP will be open ...");
}
```

The `isEnabled()` check is replaced with `tryBeginForApInfo(config)`,
which probes when the manager hasn't been brought up by `setupDisplay()`:

```cpp
if (display != nullptr) {
    Config::DisplayConfig apConfig{};
    if (display->tryBeginForApInfo(apConfig)) {
        Support::computeApPassword(deviceId.c_str(), password, sizeof(password));
        useWpa2 = true;
        ESP_LOGI(TAG, "Display responded at AP-mode entry — using WPA2-PSK");
    } else {
        ESP_LOGW(TAG, "No display responded at AP-mode entry — AP will be open");
    }
}
```

The `else if (display == nullptr)` branch becomes a single guard
(`display != nullptr`); the `display == nullptr` case still falls
through to open AP with the existing `No DisplayManager wired` warning.
The two log lines that ship today (`Display disabled in config — AP will
be open` and `open — no display detected`) collapse into the single
`No display responded at AP-mode entry — AP will be open` log on the
probe-failed branch — the source of the decision is now the probe, not
the config flag.

### D3 — `Config::DisplayConfig` is the input to `tryBeginForApInfo`

The function takes a `Config::DisplayConfig` so the rotation is
configurable (matches the design in the archived change). On the AP-mode
entry path we pass `Config::DisplayConfig{}` — the default-constructed
config, with `rotation == 0`. The normal status-display path is
independent: `setupDisplay()` calls `displayManager.begin(config, name)`
unconditionally, so a user who has enabled the status display via the
web UI still sees their saved rotation. Only the deferred bring-up path
— i.e. the path that fires when `DisplayConfig.enabled == false` — uses
the default rotation, and that's the path `tryBeginForApInfo` exists
for.

### D4 — Native test runs only the boundary that isn't hardware

`panel.begin()` is `#ifdef ARDUINO`. The native stub in
`EPaperDisplay.h:188` returns `false` from `probe()`, and the native
build of `DisplayManager` does not exist (the whole class is
`#ifdef ARDUINO`). So the native test cannot exercise the new function
directly.

Instead, the native test exercises the *contract* of the change: when
the panel is unavailable (the only thing the native build can model),
the AP comes up open. The test inspects `Network::isApOpen()` after a
synthetic `startAP()` call. `isApOpen()` doesn't exist yet — it's
added alongside `tryBeginForApInfo` as part of this change.

## Risks / Trade-offs

- **Probe cost on every AP-mode entry**: `GxEPD2::display.init()` takes
  ~1 s on a healthy panel and up to 10 s (its `_busy_timeout`) on a
  stuck one. AP mode is rare (first boot, every third failure), so this
  is acceptable. The watchdog is fed inside `panel.begin()` for exactly
  this case.
- **SPI bus contention with the sensor I2C bus**: the panel and sensors
  live on disjoint pins (SPI vs STEMMA QT I2C), so the probe cannot
  disturb the sensor bus — see the spec requirement in `display/spec.md`
  → "SPI bus does not contend with the sensor bus".
- **`isApOpen()` state lives in Network**: only meaningful in AP mode.
  In STA mode it is stale but unused. No leak, same as the archived
  design.
- **Open AP on a connected-but-unconfigured panel**: if the panel
  responds to `panel.begin()` but trips the fault guard on a subsequent
  refresh, `tryBeginForApInfo` already returned true and the AP came up
  WPA2-PSK. The password is shown on the panel by `showApInfo`. If the
  panel later fails, the AP stays WPA2-PSK — that's the right
  failure mode (the user got the password before the fault, and a
  restart re-tests the panel).

## Migration Plan

No data migration. NVS keys are unchanged. The user-visible change is
the AP security mode on factory-fresh devices with a panel connected —
they now see WPA2-PSK with the password on the panel, where they
previously saw open AP.

For a user with a previously working device:

- Boot into STA mode: no change. `startAP()` doesn't run.
- Reboot into AP mode (first boot, or every-third-failure fallback):
  the probe now runs at the moment of AP entry. A device with a panel
  connected but `DisplayConfig.enabled == false` (factory-fresh) now
  sees WPA2-PSK where it previously saw open AP.

For a user with a previously stuck panel: `panel.begin()` returns false
after the fault guard trips, the AP opens. No regression.

## Open Questions

- *Should `tryBeginForApInfo` set `enabled = true` on success so a later
  `update()` tick repaints the boot splash instead of the AP info?* No.
  The AP info screen is the right thing to show throughout AP mode, and
  `apModeActive` (already present) suppresses `update()` while it is on
  the panel. Setting `enabled` here would also mean the user has to
  disable the display via the web UI to escape from the deferred state
  if they want STA mode without a panel — currently `enabled` reflects
  the user's explicit choice and we shouldn't override it.
- *Should the probe re-run on every AP-mode entry, including the
  every-third-failure fallback?* Yes — matches the archived design and
  the spec scenario "Probe is re-run on every AP-mode entry". A panel
  that was responsive on the first entry may not be on the second.