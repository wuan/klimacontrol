## Context

The Network task's boot-cycle failure path lives in `Network::task()`
(`src/Network.cpp:415-424`):

```cpp
if (WiFi.status() != WL_CONNECTED) {
    uint8_t newFailures = config.incrementConnectionFailures();
    ESP_LOGW(TAG, "Failed to connect (failure %u/%u) - waiting before retry...",
             newFailures, AP_FALLBACK_THRESHOLD);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP.restart();
}
```

`startSTA()` itself already does the right thing internally: it tries
3 times with 15 s windows, feeds the watchdog inside the wait, and only
returns failed after all 3 attempts. So one call to `task()` already
spends ~57 s before declaring failure. After the failure, the device
waits 2 s and restarts. The AP-fallback threshold (`failures % 3 == 0`,
`src/Network.cpp:355-403`) opens AP mode for 5 minutes on the 3rd, 6th,
9th, … failure.

The effective cycle on a flaky link is:

| Boot | `wifi_failures` after | Pre-restart delay | Cumulative |
|------|-----------------------|-------------------|------------|
| 1 | 1 | 2 s | ~60 s |
| 2 | 2 | 2 s | ~120 s |
| 3 | (AP mode for 5 min) | — | — |
| 4 | 4 | 2 s | — |
| 5 | 5 | 2 s | — |
| 6 | (AP mode for 5 min) | — | — |
| … | … | … | … |

So the device spends ~2 minutes between every AP-mode window. During
that 2 minutes, the user cannot reach the device for reconfiguration
(because it is in STA mode and the link is broken). After ~6 minutes
of failure total the user is given 5 minutes of AP mode, then the cycle
repeats. Net effect: the user is given a 5-minute window to reconfigure
every ~6 minutes, with the rest of the time spent in a STA-boot
bounce that is too fast for the network to recover.

A short, transient AP outage (e.g. AP rebooting, RF interference
clearing, ISP flap) typically resolves in 30 s to 5 minutes. The
current 2 s delay does not give the AP any time to come back, so a
transient outage looks the same as a permanent one — the device
restarts over and over.

## Goals / Non-Goals

**Goals:**

- Give a transient AP outage a chance to resolve before the device
  restarts, by spacing successive restart attempts further apart.
- Preserve the existing AP-fallback policy (every 3rd failure still
  opens AP mode for 5 minutes).
- Make the schedule testable in isolation: a `static` helper that
  takes the failure count and returns a delay in ms, with no
  dependency on `Network` instance state or on FreeRTOS.
- Document the schedule in a new spec requirement so the curve is
  reviewable in the spec, not just in code.
- Add native unit tests for the helper that lock the schedule down:
  doubling, saturation, monotonicity, edge cases (`failures == 0`,
  very large counts).

**Non-Goals:**

- Change the AP-fallback threshold. The current `failures % 3 == 0`
  policy is reasonable (it gives the user a reconfiguration window
  early, then again every 3rd failure, so a transient outage does
  not deny the user a way in forever) and the user message
  acknowledges it as "reasonable". A separate change can revisit it
  if needed.
- Change the intra-session resilience behavior (the existing
  `network-wifi-resilience` spec already covers the 3×15 s attempts
  inside `startSTA()` and the 30 s/6-attempts active reconnect during
  the steady state). The backoff is a *between-boot* delay, not an
  *inside-boot* delay.
- Change the post-success reset of `wifi_failures`. The counter is
  reset to 0 on a successful association, which means a successful
  recovery from a flaky AP still resets the backoff curve. This is
  the right behavior — the curve measures *consecutive* failures, not
  lifetime failure count.
- Replace `ESP.restart()` with a software reboot loop inside the
  same boot. The cost of a fresh boot is small (~1 s in `setup()`)
  and a clean state helps when the previous boot's WiFi stack is in
  a bad shape.
- Add jitter to the backoff. The use case is "device on a flaky
  network", not "fleet of devices thundering against a single AP",
  so random jitter would only complicate the test surface without
  buying anything.

## Decisions

### Decision 1 — Doubling backoff with a 5-minute cap

**Choice**: `delay_ms(failures) = min(2000 * 2^(failures-1), 300_000)`.
First failure waits 2 s, then 4, 8, 16, 32, 64, 128, 256, 300, and the
300 s value is held for every subsequent failure.

**Rationale**: A doubling schedule is the canonical exponential
backoff. The 2 s base matches today's behavior, so the first
restart after a failure is identical to what we have now. The
5-minute cap is chosen so the device never waits *longer* than the
AP-mode window it would fall back to anyway (5 min), so the cap
behavior is consistent with the rest of the policy. After ~30
minutes of sustained failure the device is rebooting only every 5
minutes, which gives a real AP outage (typical home router reboot
takes 30-90 s) plenty of room to recover.

**Alternatives considered**:

- **Linear backoff** (`2 s + 30 s * (failures-1)`) — converges much
  slower. After 10 failures, linear gives 5 min vs. exponential's
  300 s cap; both are in the right ballpark, but linear keeps
  growing past 5 min, which is harder to reason about.
- **Geometric without cap** (`2 s * 2^(failures-1)`) — overflows
  on `uint32_t` around `failures == 32` and produces nonsense
  values (multi-minute waits, then days, then centuries). A cap is
  mandatory.
- **Fixed long delay (e.g. always 60 s)** — simple but punishes the
  first failure (60 s instead of 2 s, so a *one-off* failure now
  costs 60 s of downtime). The exponential curve keeps the first
  failure fast and only slows down after repeated failures.
- **Random jitter** — would help if the device were one of many
  against a shared AP, but the use case is a single device against
  its home AP. The determinism also makes the test surface
  cleaner.
- **AP-fallback-driven backoff** (e.g. expand the threshold
  itself) — out of scope per the Goals; if the user wants to
  revisit the threshold, that is a separate change.

### Decision 2 — `static` helper, not a free function or a member

**Choice**: `static uint32_t Network::staFailureBackoffMs(uint8_t
failures)` — a `static` member of `Network`.

**Rationale**: A `static` member keeps the helper discoverable
(`Network::staFailureBackoffMs` is greppable next to other
`Network::*` calls), avoids polluting the `Config` or `Support`
namespaces with networking-specific logic, and is testable on
native without instantiating `Network` (no `WiFi`, no
`Preferences`, no FreeRTOS). A free function in an anonymous
namespace inside `Network.cpp` would be equivalent in testability
but slightly less discoverable.

**Alternatives considered**:

- **Free function in an anonymous namespace inside `Network.cpp`** —
  works, but `Network::staFailureBackoffMs` is the more natural
  place to find it.
- **Member function on `Network`** — needs an instance, which means
  the test would have to construct a `Network` (and pull in
  `WiFi`, `Preferences`, etc.). Defeats the purpose of being a
  pure function.
- **Free function in `Support` namespace** — would require either
  moving it to `src/support/` (a new file) or declaring it in
  `Network.h`. Adds a dependency for a one-liner. Not worth it.

### Decision 3 — Log the chosen delay

**Choice**: Replace the existing `waiting before retry...` log line
with a line that includes the actual delay (e.g. `Waiting 8000 ms
before retry (failure 4/3)`).

**Rationale**: The exponential curve is otherwise invisible to
anyone reading the serial log. Surfacing the chosen delay in the
log makes the schedule observable and lets an operator confirm
the curve is in effect (and not, say, stuck at 2 s due to a
bug).

**Alternatives considered**:

- **No log change** — works, but the schedule is then opaque. The
  cost of one extra log line per failure is trivial.
- **Log the full schedule on every boot** — noisy and stale-prone
  (the schedule is a constant; the log just needs to show the
  chosen value, not the whole curve).

## Risks / Trade-offs

- **[Risk]** A persistent outage now means a longer first-recovery
  delay: the device waits 2 s, 4 s, 8 s, … before the 3rd failure
  hands off to AP mode. → **Mitigation**: The 2 s base means the
  first failure is identical to today. The increase only shows up
  after repeated failures, which is exactly when the user needs
  the network to recover (or needs to get into AP mode to
  reconfigure).
- **[Risk]** A clock that has been running for a long time may have
  its `wifi_failures` counter at a value that triggers the 5 min
  cap immediately. → **Mitigation**: The counter is reset to 0 on
  the next successful association, so the cap only matters during
  a *current* outage. The first 8 failures use the doubling
  portion of the curve; the cap only kicks in after sustained
  failure. This is the desired behavior.
- **[Risk]** A regression that hardcodes 2 s in the helper would
  silently break the curve. → **Mitigation**: The unit test asserts
  specific values for `failures = 0, 1, 2, 4, 8, 9, 12` (covering
  base, doubling, and saturation), so a regression in either
  direction fails CI.
- **[Trade-off]** The cap is the same as the AP-mode window
  (5 min). On a long outage, the device spends 5 min waiting
  between STA attempts and 5 min in AP mode, for a 10-min cycle.
  An operator could in principle want a longer cap; the cap is
  tunable but starting at 5 min matches the rest of the policy
  and the spec requirement is explicit about the value.

## Migration Plan

1. Land the change in one PR. The helper is `static`, the call
   site is one line, the spec addition is one new requirement, and
   the test is one new suite.
2. Run `pio run -e adafruit_qtpy_esp32s2` to confirm the firmware
   still builds.
3. Run `pio test -e native` to confirm all existing native tests
   still pass, plus the new `test_wifi_restart_backoff` suite.
4. Manual verification on a device with valid but flaky credentials:
   confirm the log line shows the delay doubling across successive
   failures. Confirm AP mode still fires on the 3rd, 6th, 9th, …
   failure as before.
5. Rollback is `git revert`; the change is local to `Network.cpp`,
   `Network.h`, the spec, and the new test suite.

## Open Questions

- None. The schedule is a single line, the helper is a single
  function, and the spec delta is a single new requirement. If a
  future change wants to revisit the cap or the base, the helper is
  the single place to update.
