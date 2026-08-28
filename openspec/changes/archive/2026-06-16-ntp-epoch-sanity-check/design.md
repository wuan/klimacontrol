## Context

`Network::connectSTA()` and the Network task's NTP refresh
path both treat a `true` return from `safeNtpUpdate()` as
"synced":

```cpp
// src/Network.cpp:278-280
if (safeNtpUpdate()) {
    ntpSynced = true;
    lastNtpUpdateEpoch = ntpClient.getEpochTime();
    ESP_LOGI(TAG, "NTP time: %s", ntpClient.getFormattedTime().c_str());
}
```

`safeNtpUpdate()` is just `Support::guardedCall([this] {
return ntpClient.forceUpdate(); })` — the watchdog feed is
the only added value. The success-or-failure decision
is made entirely by `NTPClient::forceUpdate()`, which is
a thin wrapper over the underlying UDP exchange. There
is no check that the resulting `_currentEpoc` is a
plausible wall-clock value. A malformed NTP response
(KOD packet, a server returning 0 in the transmit
timestamp, a corrupted packet that the library still
parses as "received") can set `_currentEpoc` to 0 or a
garbage value while `forceUpdate()` still returns `true`.

Downstream, `getCurrentEpoch()` returns that value
verbatim (`src/Network.h:217`):

```cpp
uint32_t getCurrentEpoch() const {
    return ntpSynced ? ntpClient.getEpochTime() : 0;
}
```

The MQTT publish path uses it directly as
`{"time":%u, ...}` (`src/Network.cpp:771-789`), and the
mqtt-integration spec already says `"time": 0` is the
"not yet synced" sentinel
(`openspec/specs/mqtt-integration/spec.md:54`). The bug
is that "synced" can also produce 0, so the consumer
cannot distinguish a device that hasn't synced from one
that synced to a bogus value.

The existing `ntpSynced` flag was added specifically
because NTPClient's `getEpochTime()` returns
elapsed-since-boot before any sync
(`src/Network.h:61-63`). The flag is reliable as a
"we've called forceUpdate() at least once and it said
yes" signal — but it is NOT reliable as a "the time is
correct" signal, because the library can say yes to a
bad value.

## Goals / Non-Goals

**Goals:**

- Reject any NTP sync whose resulting epoch falls
  outside a plausible range; treat that sync as failed.
- Apply the check at every NTP success site in
  `src/Network.cpp` (initial sync, unsynced-retry, and
  periodic 1-hour refresh).
- Keep the previous `lastNtpUpdateEpoch` and the
  `ntpSynced = true` state when a single periodic
  refresh returns a bogus epoch — the device is not
  put back into the unsynced state for a one-off
  anomaly.
- Increment a single counter on every bogus-epoch
  occurrence so the field can spot a misbehaving NTP
  server.
- Update the `networking` spec to make the
  plausibility check a documented part of the NTP
  contract.

**Non-Goals:**

- Changing the plausibility bounds (2020-2100 is
  generous enough for any real deployment in the
  device's expected service life).
- Switching to NTS or any other secure-NTP work.
- Surfacing `ntpBogusSyncCount` via the web UI or the
  status API in this change — the counter is for
  serial-log correlation in the field. A follow-up
  change can expose it if telemetry data shows it is
  worth surfacing.
- Changing `getCurrentEpoch()`'s contract — the
  function already returns 0 when `ntpSynced` is false,
  and the new check keeps that promise by ensuring
  `ntpSynced` is only true when the epoch is plausible.

## Decisions

### D1. Plausibility bounds: 2020-01-01 ≤ epoch ≤ 2100-01-01

```cpp
static constexpr uint32_t NTP_MIN_VALID_EPOCH = 1577836800; // 2020-01-01T00:00:00Z
static constexpr uint32_t NTP_MAX_VALID_EPOCH = 4102444800; // 2100-01-01T00:00:00Z
```

**Rationale.** Any NTP server the device can plausibly
reach in 2026 will return an epoch in the
1.6–1.8 billion range (year 2020 to ~year 2027). The
upper bound of 2100 is a generous ceiling for any
clock-skew anomaly: a real NTP response that drifts
more than 75 years into the future is by definition
malformed. 0 is the obvious "not synced" sentinel
that the mqtt-integration spec already documents; the
lower bound excludes it explicitly.

**Alternatives considered**

- *`epoch > 0` (the current de facto check).* Rejected
  — does not catch a corrupted value like
  `0xDEADBEEF` or a small-but-nonzero bogus value.
- *`epoch > some recent date like
  `1_700_000_000`.* Rejected — couples the firmware to
  a hardcoded calendar year. The 2020-01-01 lower
  bound is a stable floor that any NTP response in
  the device's service life will comfortably exceed.
- *RFC 5905 sanity ranges (e.g. reject epochs in the
  first NTP era that disagree with `gettimeofday`).*
  Rejected — NTPClient doesn't expose era information
  to the application, and the simple "year 2020 to
  2100" check is sufficient for the field-deployment
  threat model.

### D2. Inline helper `isNtpEpochPlausible(uint32_t)`

```cpp
inline bool isNtpEpochPlausible(uint32_t epoch) {
    return epoch >= NTP_MIN_VALID_EPOCH && epoch <= NTP_MAX_VALID_EPOCH;
}
```

Lives in `src/Network.h` next to the existing
`getCurrentEpoch()` declaration. Header-inline so
unit tests can exercise it without linking the full
`Network` class (which is `ARDUINO`-gated and pulls
in the WiFi stack).

**Rationale.** The check is used at three call
sites; an inline helper keeps the call sites
readable and gives the unit test a single function
to cover.

### D3. Failure-handling: site-dependent

The three success sites have different semantics, so
the failure path is not uniform:

- **Initial sync (`src/Network.cpp:278-280`)** — the
  device is not yet synced. A bogus epoch means we
  stay unsynced; `getCurrentEpoch()` will continue
  returning 0; the periodic 1-minute retry loop in
  `Network::loop()` will keep trying. Log
  `ESP_LOGE` once with the bogus value; no need to
  re-arm any retry timer (the existing one fires
  every minute via `lastNtpRetry`).

- **Unsynced-retry (`src/Network.cpp:662-664`)** —
  same shape as the initial sync: stay unsynced,
  the existing 1-minute retry timer is already
  armed. Log `ESP_LOGE` with the bogus value; no
  additional state change.

- **Periodic refresh (`src/Network.cpp:642-658`)** —
  the device is already synced and the previous
  `lastNtpUpdateEpoch` is still valid (just stale).
  If the new value is bogus, **keep** the previous
  epoch and keep `ntpSynced = true`. The next 1-hour
  interval will try again. Log `ESP_LOGE` and call
  `reportInternetFailure()` so the existing
  internet-failure counter increments, matching the
  "NTP update failed" branch one block above. This
  keeps the existing "stay synced on transient
  failure" semantics for the periodic-refresh case.

**Rationale.** Putting the device back into the
unsynced state for a one-off bogus refresh would
re-trigger the 1-minute retry loop, which would
both thrash the serial log and waste UDP exchanges.
The cost of "keep the old value for another hour"
is bounded: the worst case is a 1-hour window of
stale-but-correct timestamps before the next
refresh, which is exactly what the periodic-refresh
code already accepts when `safeNtpUpdate()` returns
`false` (`src/Network.cpp:656`: "Stay synced — the
previous epoch is still usable, just stale.").

### D4. Single `ntpBogusSyncCount` counter, log-only

```cpp
uint32_t ntpBogusSyncCount = 0;
```

Private to `Network`. Increments at every site that
sees a `safeNtpUpdate() == true` but
`isNtpEpochPlausible(epoch) == false` result. Not
exposed via the web API or the JSON status response
in this change. The `ESP_LOGE` line is the primary
signal; the counter is a serial-log correlation aid
(e.g. "we saw 3 bogus epochs in the last hour" is
easier to spot in a long log when the counter is
also printed in the `ESP_LOGE` line).

**Alternatives considered**

- *Surface `ntpBogusSyncCount` in `/api/status`
  next to the existing failure counters.* A
  reasonable follow-up, but the field deployment
  has not yet shown this metric is worth UI
  real estate. Keep it as a developer signal for
  this change; add the API field in a follow-up if
  telemetry justifies it.
- *Reset the counter on every reboot.* Rejected —
  persistent lifetime counters are more useful for
  "did this device ever see a bad NTP server"
  questions.

### D5. Spec update: new scenario + tighten existing

The `networking` spec's "NTP time synchronization"
requirement gets:

- A new scenario **"Bogus epoch is rejected"**:
  `safeNtpUpdate()` returns `true` but
  `ntpClient.getEpochTime()` returns a value
  outside the plausibility range; `ntpSynced`
  SHALL remain `false` (or the previous
  `lastNtpUpdateEpoch` SHALL be kept, depending
  on the call site), `ntpBogusSyncCount` SHALL
  increment, and an `ESP_LOGE` line SHALL be
  emitted with the bogus value.

- A tightened "First sync after association"
  scenario: the existing `WHEN` clause is widened
  to require both `forceUpdate()` returning
  `true` AND the resulting epoch being plausible;
  the `THEN` clause stays the same (record the
  synced epoch, expose it via `getCurrentEpoch()`).

**Rationale.** The new scenario documents the new
failure path explicitly. The tightened existing
scenario prevents a future regression where the
plausibility check is removed and the spec
silently reverts to trusting the boolean return.

## Risks / Trade-offs

- **Risk:** A real NTP server returns an epoch
  that's just outside the bounds (e.g. firmware
  shipped in 2120, or a system clock that drifted
  decades into the future). **→** Mitigation: the
  upper bound of 2100-01-01 gives a 75-year
  margin; the firmware is unlikely to be in
  service that long. The lower bound of 2020-01-01
  is similarly a 6-year margin (assuming a 2026
  deployment).
- **Risk:** The plausibility check rejects a
  legitimate value during the roll-over second
  (e.g. the device is at 1577836800 exactly on
  2020-01-01T00:00:00Z). **→** Mitigation: the
  check is `>= NTP_MIN_VALID_EPOCH` (inclusive),
  so the exact boundary is accepted. A real
  roll-over concern is decades away; not worth a
  more complex check.
- **Risk:** A misbehaving NTP server returns
  plausible-but-wrong values (e.g. 30 years in
  the future) and the check passes. **→**
  Mitigation: this is a NTP-server-trust problem
  outside the firmware's threat model. The bounds
  are wide enough to catch the corruption modes
  this change is targeting (zero, near-zero,
  obviously bogus); narrower bounds (e.g. within
  ±1 day of "now") would create false positives
  on devices with no RTC backup across a power
  loss.
- **Risk:** `ntpBogusSyncCount` grows unboundedly
  if a device is stuck with a bad NTP server.
  **→** Acceptable; the counter is 32 bits and
  wraps in ~136 years at 1 increment per second.

## Migration Plan

This is a refactor of the NTP success path; no
runtime deploy step is needed beyond rebuilding
and re-flashing.

1. Apply the helper + bounds + counter in
   `src/Network.h`.
2. Apply the three call-site updates in
   `src/Network.cpp`.
3. Add unit tests for `isNtpEpochPlausible()`.
4. Update the `networking` spec delta.
5. Build for ESP32 (`pio run -e
   adafruit_qtpy_esp32s2`).
6. Run native tests (`pio test -e native`).
7. Archive the change.

**Rollback.** Revert the source + spec commits.
`safeNtpUpdate()`'s contract is unchanged;
`getCurrentEpoch()`'s contract is unchanged. The
only behaviour change is that bogus epochs are
now rejected, which is the bug fix itself.

## Open Questions

None. The bounds, the helper, the per-site
failure handling, and the counter are all
directly derivable from the existing NTP
success-path code and the existing
mqtt-integration "time: 0 sentinel" contract.
