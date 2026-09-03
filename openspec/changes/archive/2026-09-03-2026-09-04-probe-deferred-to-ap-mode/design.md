## Context

The previous change shipped `EPaperDisplay::probe()` called from
`main.cpp::setup()`, with the result stored in `Network::apPassword`
and read back by `Network::startAP()`. That put the detection cost
on every boot, even though the result is only consumed in AP mode
(rare). It also depended on a hand-rolled BUSY-pin probe that turned
out to be unreliable on the Waveshare 1.54" V2 — the panel would
respond to the reset pulse (BUSY=LOW within 1 ms) but never
release BUSY=HIGH within the 10 s budget.

The probe is only ever consumed by `Network::startAP()`. Moving it
into `startAP()`:

1. Pays the probe cost only on the path that actually needs it
   (AP-mode entry, which is rare: first-boot-without-WiFi or
   every-third-failed-reconnection).
2. Lets the probe use the panel state at the moment it matters
   rather than guessing from the boot-time state.
3. Lets `tryBeginForApInfo()` delegate to the proven
   `GxEPD2::display.init()` path (`EPaperDisplay::begin()`), which
   is what the rest of the firmware uses to bring the panel up.

The user's last log line shows the probe timing out with the panel
stuck BUSY=LOW from a previous boot. A power cycle clears that
state; once the panel is fresh, `display.init()` (and therefore
`panel.begin()`) brings it up cleanly. The new architecture keeps
this fallback: a stuck panel still returns false from
`tryBeginForApInfo()`, the AP opens, and the user can configure WiFi
without needing the panel.

## Goals / Non-Goals

**Goals:**

- The probe runs only when entering AP mode, not on every boot.
- The probe decision is correct: WPA2-PSK when a panel responds,
  open AP otherwise.
- The decision is available to `/api/ap-info` (the captive portal
  page) so it can render the right help text.
- The proven `GxEPD2::display.init()` path is the only thing the
  probe touches. No hand-rolled BUSY dance.

**Non-Goals:**

- A pre-flight probe at boot. STA-mode boots should not pay this
  cost.
- A retry mechanism for stuck panels. If the panel is in a bad state
  (mid-refresh interrupted by a previous power cycle, hardware
  fault, flaky wiring), the only recovery is to power-cycle the
  device. Adding software retries would mask the underlying problem.
- A new endpoint to surface the AP password. The password is
  deterministic from the device id; `/api/ap-info` computes it on
  demand when the captive portal page needs it.

## Decisions

### D1 — Probe moved into `Network::startAP()`, not split across calls

`startAP()` runs the probe, computes the password, and brings up
the AP, all in one call. The captive portal endpoint reads the
decision through `Network::isApOpen()`. No state needs to be
carried from `setup()`.

```cpp
void Network::startAP() {
    // ... mode / SSID ...

    bool useWpa2 = false;
    char password[AP_PASSWORD_BUF_SIZE] = "";

    if (display != nullptr) {
        const Config::DisplayConfig apConfig{};
        if (display->tryBeginForApInfo(apConfig)) {
            Support::computeApPassword(deviceId.c_str(), password, sizeof(password));
            useWpa2 = true;
        } else {
            ESP_LOGW(TAG, "No e-paper panel responded — AP will be open");
        }
    }

    if (useWpa2) {
        apIsOpen = false;
        WiFi.softAP(ap_ssid.c_str(), password);
        // ... paint AP info on panel ...
    } else {
        apIsOpen = true;
        WiFi.softAP(ap_ssid.c_str());
    }
}
```

The decision (WPA2-PSK or open) lives entirely inside `startAP`,
which is the only consumer. `Network::isApOpen()` is read-only state
for `/api/ap-info`.

### D2 — `tryBeginForApInfo()` delegates to `panel.begin()`

The previous version of `tryBeginForApInfo()` did a hand-rolled
BUSY-pin probe first (`panel.probe(250)`), then called
`panel.begin()`. The hand-rolled probe timed out on the Waveshare
1.54" V2 — the panel stayed BUSY=LOW past the budget even though
it was responsive. The proven path is `panel.begin()` itself, which
calls `GxEPD2::display.init()` and returns `!faulted`. A healthy
panel returns within ~1 s; a stuck panel trips the fault guard
after 3 consecutive BUSY timeouts.

The simplified function:

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

### D3 — `/api/ap-info` computes the password on demand

The password is deterministic from the device id (FNV-1a + fixed
salt). `/api/ap-info` computes it from the device id at request
time — no storage needed in `Network`, no slot helper, no carry-over
state from `startAP()`.

```cpp
server.on("/api/ap-info", HTTP_GET, [this](AsyncWebServerRequest *request) {
    const String deviceId = DeviceId::getDeviceId();
    const String ssid = Constants::AP_SSID_PREFIX + deviceId;

    JsonDocument doc;
    doc["ssid"] = ssid;
    if (network.isApOpen()) {
        doc["open"] = true;
    } else {
        char password[Network::AP_PASSWORD_BUF_SIZE];
        Support::computeApPassword(deviceId.c_str(), password, sizeof(password));
        doc["password"] = password;
        doc["open"] = false;
    }
    // ... serialize ...
});
```

This is only registered when the device is in CONFIG route set
(i.e., AP mode), so the endpoint does not exist in STA mode. The
`isApOpen()` flag is set by `startAP()` and is meaningful only in
that context.

### D4 — `Network::setApPassword`/`getApPassword` and the slot helpers are removed

With the new architecture, no caller of `setApPassword`/`getApPassword`
exists in production. `Support::setApPasswordSlot`/
`getApPasswordSlot` were the testable boundary of that API; with
the API removed, the helpers and their tests are gone too. The
547 surviving native tests cover the rest of the behaviour.

## Risks / Trade-offs

- **Probe at AP-time only**: if the panel recovers between the AP-mode
  entry and the next AP-mode entry (rare but possible if the user
  re-flashes), the probe correctly re-runs. The probe cost is bounded
  by `GxEPD2`'s `_busy_timeout` (10 s), which the network task can
  afford because AP mode is rare.
- **`isApOpen()` state lives in `Network`**: only meaningful in AP
  mode. In STA mode it is stale but unused. The endpoint that reads
  it is only registered in AP mode, so no leak.
- **Captive portal page on the device**: the HTML is unchanged; it
  reads `info.password` / `info.open` exactly as before. The
  `/api/ap-info` shape is unchanged.
- **Stuck panel still recovers to open AP**: a panel stuck BUSY=LOW
  from a previous boot returns `false` from `panel.begin()` after
  the fault guard trips, `tryBeginForApInfo` returns false, and the
  AP opens. No regression versus the previous architecture's
  behaviour.

## Migration Plan

No data migration. NVS keys are unchanged. The API shape on the
wire (`/api/ap-info` JSON) is unchanged.

For a user with a previously working device:
- Boot into STA mode: no change. The probe no longer runs at boot,
  but the device doesn't need it in STA mode.
- Reboot into AP mode (first boot, or after 3 failed reconnections):
  the probe runs at the moment of AP entry, instead of at boot.

For a user whose panel is currently stuck from a previous boot:
- Power cycle the device. The panel comes back in a fresh state
  (`BUSY=HIGH`, idle). The probe at the next AP entry succeeds.

## Open Questions

- *Should the probe be retried inside `startAP()` if the first
  attempt fails?* No. A single failure is enough to fall back to
  open AP. Retrying would either mask the underlying hardware issue
  or delay the AP by N× the probe budget.
- *Should the captive portal page show "open network" instead of the
  password when AP is open?* Yes, via the `open` flag. The HTML
  renders the right help text based on the flag.
