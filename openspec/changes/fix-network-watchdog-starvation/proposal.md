## Why

The Network task is registered with the ESP-IDF task watchdog (30 s timeout,
panic on trigger) and the bulk of its work is fine — the `startSTA()` polling
loop and the `configureUsingAPMode()` AP-wait loop both call
`esp_task_wdt_reset()` on every iteration. The exception is the three
`ntpClient.forceUpdate()` call sites in `src/Network.cpp` (lines 262, 584,
601). NTPClient's library implementation (`forceUpdate` → `sendNTPPacket`
followed by a `do { delay(10); parsePacket(); } while (cb == 0)` loop with
a 1000 ms internal timeout) is supposed to bound the call, but the call is
not under our control: the underlying `WiFiUDP::parsePacket()` can be
delayed by a busy lwIP stack, the 1 s internal timeout is not configurable,
and a degraded link can cause the call to sit on a retransmit for tens of
seconds. If `forceUpdate()` blocks past the 30 s TWDT, the panic handler
forces a hard reboot mid-task, which can land inside an NVS write and
silently corrupt the config partition.

The same risk applies to any other blocking external call made from the
Network task that does not already feed the watchdog on every iteration.
`startSTA()` is safe (its own loop resets the watchdog every 500 ms), the
captive portal's `processNextRequest()` is per-packet non-blocking, and
MQTT is already gated by `TCP_CONNECT_TIMEOUT_MS` in `MqttClient.cpp`, but
the policy is not documented and is one regression away from going wrong.

## What Changes

- Introduce a small `Network::safeNtpUpdate()` helper in
  `src/Network.cpp` that calls `esp_task_wdt_reset()` before and after
  `ntpClient.forceUpdate()`. The helper returns the same `bool` so the
  existing call sites in `startSTA()` and the periodic update loop drop in
  one-for-one.
- Document the policy with a new requirement in the `networking` spec:
  *Every external (non-loopback) network call made from the Network task
  SHALL either bound its own time (socket timeout, library timeout) or
  feed the TWDT before and after the call.* The NTP helper is the first
  implementation; future blocking calls in the same task must follow the
  same pattern.
- Add a comment in `Network::startSTA()` and the two NTP sites in
  `Network::task()` that points to the helper and explains why it exists.
- Add a unit test (native) for the helper that asserts it forwards
  `forceUpdate()`'s return value and (where testable) calls
  `esp_task_wdt_reset()` even if the NTP call throws or returns false. The
  TWDT API is not available on native, so the test stubs it via a small
  seam in the helper (e.g. a `static std::function<void()> wdtResetHook`
  that defaults to `esp_task_wdt_reset` under `ARDUINO` and a no-op on
  native).
- Audit the rest of the Network task for similar blocking calls; add the
  helper at any other site that meets the criterion (the audit is
  expected to find no other violations, but the search is documented in
  the design).

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `networking`: add a new **Network task blocking-call safety** requirement stating that external network calls in the Network task MUST be guarded (either by an internal timeout or by feeding the TWDT before and after the call). The NTP update path is the canonical example. Existing `NTP time synchronization` and `STA mode association` requirements are updated to mention the new safety contract.
- `system-architecture`: tighten the **FreeRTOS task structure** requirement to make the watchdog-feeding contract explicit for any blocking call, not just the per-iteration `esp_task_wdt_reset()` that the existing scenario already covers.

## Impact

- **Code**
  - `src/Network.cpp` — add `safeNtpUpdate()` helper, replace the three
    `ntpClient.forceUpdate()` call sites with the helper, add a comment
    block in `startSTA()` and the periodic loop.
  - `src/Network.h` — declare the private helper. The test seam
    (`wdtResetHook`) is `private static` so it does not expand the public
    API surface.
- **APIs**: no public signature change. `Network::forceNtpUpdate()` is a
  private helper; the existing public API is untouched.
- **Behavior**: in the success path, identical. In the degraded-link
  path, the TWDT is fed around the NTP exchange, so a hung NTP call no
  longer panics the device.
- **Dependencies**: none. The helper uses only the existing
  `esp_task_wdt_reset` API and the NTPClient instance.
- **Tests**: new `test/test_network_ntp_watchdog/` suite that:
  - calls the helper and asserts it returns the NTPClient's return value
    (both true and false paths);
  - asserts `wdtResetHook` is called at least twice per invocation
    (before and after the NTP call);
  - asserts `wdtResetHook` is called even when `forceUpdate()` returns
    false;
  - exercises the `wdtResetHook` install/reset seam to verify it
    composes correctly.
