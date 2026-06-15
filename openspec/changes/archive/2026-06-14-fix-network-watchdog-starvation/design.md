## Context

The Network task (`Network::task()` in `src/Network.cpp`) is registered with
the ESP-IDF task watchdog (`esp_task_wdt_add(NULL)` at the top of the
task body) with a 30-second timeout, panic on trigger. The TWDT is fed
once per task-loop iteration; in the steady state, every iteration is
short, so the watchdog never gets close to firing.

The risk appears at three `ntpClient.forceUpdate()` call sites in
`src/Network.cpp`:

- Line 262, inside `startSTA()` — runs immediately after WiFi
  association, just before the first MQTT publish.
- Line 584, inside the periodic update loop in `Network::task()` — runs
  once an hour after the first successful sync.
- Line 601, the same loop — runs at most once a minute when NTP has not
  yet synced.

NTPClient's `forceUpdate()` is documented to bound itself at ~1 s
(`do { delay(10); parsePacket(); } while (cb == 0)` with `timeout > 100`
returning `false`). On a healthy link this returns in well under a second.
On a degraded link the `WiFiUDP::parsePacket()` call can sit in the lwIP
stack for tens of seconds (retransmits, ARP resolution, etc.), and the
internal 1 s timeout does not help because the stack is busy *before*
`parsePacket()` even returns. The result: a single call can occupy the
Network task for the full 30 s TWDT, and on a transiently hung link the
panic handler fires `ESP.restart()` while the task is mid-exchange. If
the panic happens to land inside a Preferences write (the previous
`config.saveWiFiConfig()` call, the per-field `update*()` calls, etc.)
the NVS partition can be torn on the next mount.

The same class of risk applies to any blocking external call in the
Network task. Today, the rest of the task is fine:

- `WiFi.begin()` in `startSTA()` is non-blocking on ESP32 — it kicks off
  the association asynchronously and returns immediately.
- The association polling loop in `startSTA()` calls
  `esp_task_wdt_reset()` every 500 ms.
- `configureMDNS()` is a local in-memory setup after the link is up.
- `dnsServer.processNextRequest()` (the captive portal handler) is
  per-packet non-blocking; it returns after handling the current request
  or returns immediately if there is no request to process.
- `MqttClient::loop()` and the MQTT pre-connect (`wifiClient.connect(...)`)
  are already gated by `TCP_CONNECT_TIMEOUT_MS` in `src/MqttClient.cpp`.

So the only outstanding site is the NTP update. The proposal is to wrap
it in a small helper that feeds the watchdog on both sides of the call
and to make the pattern explicit in the spec so a future contributor who
adds a new external call to the Network task is on notice.

## Goals / Non-Goals

**Goals:**

- Make the three NTP call sites robust against a hang in
  `WiFiUDP::parsePacket()` or any other layer below NTPClient.
- Document the pattern in a new spec requirement so future blocking
  calls in the Network task follow the same contract.
- Add a unit test seam that lets native tests assert the watchdog is fed
  even when the underlying NTP call returns false or throws.
- Make the audit explicit: list the other blocking call sites in the
  Network task and confirm each one already follows the policy.

**Non-Goals:**

- Replace NTPClient with a different NTP library.
- Bypass the TWDT (the 30 s budget stays; we just feed it correctly).
- Add a generic "guarded call" macro that wraps arbitrary code — the
  helper is intentionally small and NTP-specific.
- Refactor `MqttClient` or `CaptivePortal` — they already meet the
  policy.
- Change the NTP retry cadence (still 1 h synced, 1 min unsynced) or
  the NTP server list.

## Decisions

### Decision 1 — Wrap NTP in a private helper, not inline the WDT resets

**Choice**: Add a `bool Network::safeNtpUpdate()` helper in
`src/Network.cpp` that calls `esp_task_wdt_reset()`, invokes
`ntpClient.forceUpdate()`, calls `esp_task_wdt_reset()` again, and
returns the NTPClient result. Replace the three call sites one-for-one.

**Rationale**: The watchdog-feeding pattern is a one-line pattern but
appears in three places today and will appear in more as the firmware
grows. A named helper makes the policy grep-able (`safeNtpUpdate`),
centralizes the contract (one place to update if the policy changes),
and turns the call sites into intent-revealing code: `if
(network.safeNtpUpdate()) { ... }`.

**Alternatives considered**:

- **Inline `esp_task_wdt_reset()` at each call site** — works, but
  three call sites today and more to come means a one-line policy
  decision is multiplied into N copies. A future regression that drops
  one reset is silent.
- **`RAII` watchdog-feeding scope guard** — over-engineering for one
  call; the pattern is small enough that a function is clearer than a
  `struct ScopedWdtFeed { ~ScopedWdtFeed() { esp_task_wdt_reset(); } }`
  plus a variable at every call site.
- **Move the WDT reset into a `vTaskDelay` poll loop inside the
  helper** — replaces NTPClient's poll loop with our own, and
  NTPClient's poll is what we want to keep (it does the
  `parsePacket`/`read` dance correctly). The helper only adds WDT
  feeding on the outside.

### Decision 2 — A static `wdtResetHook` for native tests

**Choice**: The helper goes through a `static std::function<void()>
wdtResetHook` that defaults to `esp_task_wdt_reset` under `ARDUINO` and
a no-op on native. Tests can swap the hook to count invocations.

**Rationale**: The TWDT API is not available on native, so a unit test
that exercises the helper on the host needs a seam. A `std::function`
keeps the call site identical between firmware and native — the only
difference is which function is bound. The hook is `private static`
so it is not part of the public API, and tests that need to reset it
go through a `static` `setWdtResetHook(...)` and `resetWdtResetHook()`
pair to avoid test-order coupling.

**Alternatives considered**:

- **`#ifdef ARDUINO` around the whole hook mechanism** — would leave
  the helper un-testable on native. The hook indirection is cheap
  (one indirect call) and keeps the test path uniform.
- **A `#define` macro that the test redefines** — magic; the
  `std::function` is the C++-idiomatic version of the same idea.
- **A virtual method on `Network` overridden by a test subclass** —
  `Network` is not designed for subclassing (it owns non-virtual
  resources), and adding a virtual method for tests is a much larger
  change than a `std::function` seam.

### Decision 3 — Update the spec, not just the code

**Choice**: Add a new **Network task blocking-call safety** requirement
to the `networking` spec, and tighten the **FreeRTOS task structure**
requirement in `system-architecture` to cover the case of a blocking
call that exceeds the per-iteration budget.

**Rationale**: The fix is local to NTP today, but the *risk* is
generic. A spec requirement is the place future contributors will
look when they are adding a new external call to the Network task;
without it, the policy lives only in the helper's name, and the
helper's name disappears the moment a different kind of blocking call
is added.

**Alternatives considered**:

- **A code comment instead of a spec requirement** — code comments
  rot. The `system-architecture` spec already has a watchdog
  requirement; expanding it to cover blocking calls is a small, scoped
  addition.
- **A `docs/` markdown file** — the project does have a `docs/`
  directory, but OpenSpec is the project's source of truth for
  behavior contracts. A spec delta is the right surface.

### Decision 4 — Audit, but don't refactor

**Choice**: Walk the Network task once and call out every external
call. Document the audit in the design (this section) and the tasks
file. Do not refactor anything that already meets the policy.

**Rationale**: The proposal is "fix the NTP site + document the
pattern". Refactoring `startSTA()`'s polling loop or rewriting
`MqttClient::loop()` to add WDT resets it does not need would
balloon the change. The audit's job is to confirm the policy is met
elsewhere, not to change the code.

**Alternatives considered**:

- **Refactor every call site to go through a single
  `guardedNetworkCall()` template** — premature abstraction. One
  helper for the only site that needs it; if a second one shows up
  in the same change, we generalize.

## Audit of other Network task call sites

| Site | What it does | Policy met? |
|------|--------------|-------------|
| `startSTA()` — `WiFi.disconnect(false)` (line 172) | Local ESP-IDF call, returns quickly | Yes (trivial) |
| `startSTA()` — `WiFi.begin(ssid, password)` (line 215) | Kicks off async association; returns immediately | Yes (non-blocking by design) |
| `startSTA()` — polling loop (lines 217-226) | `vTaskDelay(500)` + `esp_task_wdt_reset()` per slot | Yes (fed every 500 ms) |
| `startSTA()` — `WiFi.disconnect(false)` on backoff (line 233) | Local call | Yes |
| `startSTA()` — `configureMDNS()` (line 251) | In-memory setup after link is up | Yes |
| `startSTA()` — `ntpClient.forceUpdate()` (line 262) | **External UDP, library timeout 1 s, not in our control** | **No — needs helper** |
| `startSTA()` — `mqttClient = std::make_unique<MqttClient>()` (line 272) | Local construction | Yes |
| `startSTA()` — `mqttClient->begin(mqttConfig)` (line 274) | MqttClient::begin configures its own pre-connect timeout | Yes (out of scope) |
| `configureUsingAPMode()` — wait loop (line 294) | `esp_task_wdt_reset()` per 100 ms | Yes |
| `CaptivePortal::handleClient()` (line 296) | `dnsServer.processNextRequest()` — non-blocking per request | Yes |
| `task()` — `WebServerManager::begin()` on mode change | Local setup, returns quickly | Yes |
| `task()` — `WebServerManager::loop()` per iteration | Async; non-blocking | Yes |
| `task()` — `ntpClient.forceUpdate()` (lines 584, 601) | **External UDP** | **No — needs helper** |
| `task()` — `MqttClient::loop()` per iteration | Bounded by `TCP_CONNECT_TIMEOUT_MS` | Yes |

The audit confirms NTP is the only outstanding site. The change is
therefore minimal: one helper, three call-site updates, one new spec
requirement.

## Risks / Trade-offs

- **[Risk]** Feeding the watchdog around `forceUpdate()` extends the
  effective TWDT budget for that call, but does not bound the call
  itself — a hang in lwIP can still let the call sit for an unbounded
  time, and any other work scheduled for that loop iteration is
  blocked. → **Mitigation**: The hung call does not corrupt NVS
  (Preferences is only written from a few well-defined call sites,
  none of which overlap with NTP), and the next iteration of the
  Network task loop will retry. The only loss is a brief stall in MQTT
  publishing and NTP sync, both of which are best-effort by design.
- **[Risk]** The `std::function<void()>` indirection adds a tiny
  per-call overhead (one indirect call vs. one direct call). →
  **Mitigation**: The NTP call is invoked at most once a minute when
  unsynced and once an hour when synced; a few nanoseconds per call
  is irrelevant.
- **[Risk]** A future contributor might add a blocking call to the
  Network task without reading the spec or noticing the helper. →
  **Mitigation**: The spec change makes the policy greppable
  (`Network task blocking-call safety`), the helper's name makes the
  policy discoverable in code (`safeNtpUpdate` next to any future
  guarded call is a strong hint), and the new test in
  `test_network_ntp_watchdog` documents the contract.
- **[Risk]** The NTP library's internal 1 s timeout is not
  configurable; if a future library version changes the timeout, the
  helper's behavior changes silently. → **Mitigation**: This change
  does not depend on the library's internal timeout — we feed the
  watchdog before and after regardless of how long the call took. The
  audit does not need to be re-run for a library version bump.

## Migration Plan

1. Land the change in one PR. The helper is private; the public API
   is unchanged; NVS and HTTP endpoints are unaffected.
2. Run `pio run -e adafruit_qtpy_esp32s2` to confirm the firmware
   still builds.
3. Run `pio test -e native` to confirm all existing native tests
   still pass, plus the new `test_network_ntp_watchdog` suite.
4. Manual verification: place the device on a network where the NTP
   server is unreachable (firewall block on UDP/123) and confirm the
   device does not panic-reboot after 30 s. The serial log should
   show repeated `NTP update failed` lines instead of a panic
   reboot.
5. Rollback is `git revert`; the change is local to `Network.cpp`,
   `Network.h`, and the new test suite.

## Open Questions

- None. The NTP fix is local, the spec delta is mechanical, and the
  audit confirms the rest of the Network task already meets the
  policy.
