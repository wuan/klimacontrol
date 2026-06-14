## Context

The firmware runs FreeRTOS with a dedicated Network task on Core 1. WiFi state was previously managed by:
- A 15 s blocking wait in `startSTA()` for the initial association.
- `WiFi.setAutoReconnect(true)` for any subsequent drops.
- A 1 s polling loop that only logged state transitions.

Field reports showed devices going offline after hours of uptime and never recovering until power-cycled. Logs offered no clue why, because `WiFi.status()` only exposes the current state — not the reason for a transition. ESP-IDF surfaces reason codes (`esp_wifi_types.h`) but only through the event callback API, which the firmware was not using.

Constraints:
- The Network task already runs the webserver, MQTT loop, NTP, status LED, and watchdog reset. Recovery logic must not block the 1 s cadence beyond what is acceptable for the LED (≈ 100 ms tolerance).
- The existing failure counter / AP fallback (`incrementConnectionFailures` → AP mode every 3 boots) must still work — recovery is in-session, not a replacement for the boot-time strategy.
- Memory budget is tight on the ESP32-S2; no heap allocations on the recovery path.

## Goals / Non-Goals

**Goals:**
- Surface disconnect reason codes in logs so the next failure is diagnosable.
- Recover from `BEACON_TIMEOUT` / `ASSOC_EXPIRE` / `AUTH_EXPIRE` disconnects that Arduino's auto-reconnect does not handle.
- Avoid restart loops on transient boot-time AP issues by retrying in-session before incrementing the failure counter.
- Bound the worst case: if active reconnect cannot recover, fall back to a clean device restart.

**Non-Goals:**
- Investigating or fixing root causes of disconnects (router behavior, sleep-mode interactions). Diagnostics first; root-cause work is a follow-up driven by what the logs reveal.
- Replacing `setAutoReconnect(true)` — it stays as the first line of defense for common cases.
- Changing the AP-fallback policy or the boot-time failure counter.
- Adding new configuration knobs for the recovery thresholds (the 30 s / 6-attempt values are tuned for the typical AP and not exposed via API).

## Decisions

**Three-layer recovery model.**
Layer 1: Arduino `setAutoReconnect(true)` — unchanged, handles common transients.
Layer 2: Active forced reconnect after 30 s down (`WiFi.disconnect(false)` + `WiFi.reconnect()`) — recovers cases where layer 1 silently gave up.
Layer 3: `ESP.restart()` after 6 failed layer-2 attempts (~3 min) — last resort for deep stack state.

*Why layered:* Each layer is cheap if it works; only escalate when the previous layer fails. Skipping layer 1 (always doing active reconnect) would generate unnecessary radio traffic; skipping layer 3 (waiting forever) would leave the device offline indefinitely.

**Event-driven disconnect detection, polled recovery.**
The `WiFi.onEvent()` handler stamps `lastWifiDisconnectMs` from the WiFi event task; the Network task reads it from the 1 s loop to decide when to trigger active reconnect.

*Why split:* Recovery actions (`WiFi.disconnect`, `WiFi.reconnect`, `ESP.restart`) are not safe to invoke from the event callback (it runs in a high-priority system task and blocking calls can stall the WiFi stack). Stamping a timestamp is non-blocking; the recovery decision is made in the lower-priority Network task. Volatile scalars are atomic on the ESP32-S2 (32-bit aligned), so no mutex is needed.

**3 × 15 s retry loop in `startSTA()` with 3 s backoff.**
54 s worst-case boot delay before AP fallback, vs. the prior 15 s.

*Why this shape:* A single 45 s wait would not retry the underlying `WiFi.begin()` — some failure modes (DHCP race, AP channel hop) need a fresh association attempt. Three short attempts give three independent chances. 3 s backoff between attempts avoids hammering an AP that may be rate-limiting.

**Disconnect-reason translation table.**
Inline `switch` on common `WIFI_REASON_*` constants in an anonymous namespace, returning string literals. Unknown reasons render as `OTHER`.

*Why not just print the integer:* Reason codes are used routinely during triage; `BEACON_TIMEOUT` is immediately recognizable, `reason=200` is not. Translation is cheap, lives next to the logging code, and the unrecognized-code fallback preserves the integer.

## Risks / Trade-offs

- **Forced reconnect during legitimate brief outages may slow recovery.**
  If the AP comes back at 25 s, layer 1 might have recovered at 26 s; instead we wait until 30 s and force a fresh association cycle. → Mitigation: 30 s threshold is generous — most real Arduino auto-reconnect recoveries complete in < 10 s.

- **`ESP.restart()` after 6 failed reconnects loses MQTT publish state.**
  In-flight measurements that have not yet been published are dropped. → Mitigation: Already the current behavior for any restart path; the alternative (offline indefinitely) is strictly worse.

- **Boot delay grows from 15 s to ~54 s when WiFi is unreachable.**
  Slower fallback to AP mode after a misconfigured SSID. → Mitigation: AP fallback is only triggered every 3rd boot failure anyway; an extra ~40 s on each of those is acceptable for a one-time reconfig event.

- **Sleep-mode interaction is still untested.**
  The user reports that sleep-mode influence on disconnects is "unclear". This change adds the diagnostics needed to investigate but does not itself fix any sleep-mode-specific bug. → Mitigation: This is intentional — Goals explicitly defers sleep-mode root cause to a follow-up driven by what the new logs reveal.
