# Tasks: Diagnose the body-POST 501

Ordered. Step 1 gates everything else — without logs the rest is guesswork.

## 1. Close the diagnostic gaps

Logging itself works — use `pio device monitor -e adafruit_qtpy_esp32s2`, not a
raw serial client. What is missing is narrower:

- [x] Register `AccessLogger` with the server (`addMiddleware`). Done in the
      `WebServerManager` constructor, not `setMode()`: `AsyncWebServer::reset()`
      clears rewrites and handlers but **not** middlewares
      (`WebServer.cpp:197`), and `addMiddleware()` appends without de-duplicating
      (`Middleware.cpp:23`), so per-mode registration would stack a copy on
      every mode flip
  - [x] Verified on hardware — every request now logs client IP, URL, method,
        elapsed ms and status, across 200/400/403/404
  - [x] **Signature to look for when the 501 recurs:** the middleware runs
        `next()` and then reads `getResponse()`, but the 501 is substituted
        later inside `_send()`. So a genuine occurrence logs
        `(no response)`, *not* `501` — that distinguishes "handler produced
        nothing" from any handler that deliberately returns 501
- [ ] Add a `debug` PlatformIO environment carrying `-DCORE_DEBUG_LEVEL=4`, so
      `ESP_LOGD` exists without hand-editing flags
- [ ] Note for boot-time work: the monitor drops when the device reboots,
      because native USB CDC re-enumerates. Reattach after reset to capture
      `Reset reason:`

## 2. Instrument the request lifecycle

- [ ] Log entry and exit of the body callback, with `index`, `len`, `total`
- [ ] Log the `DeserializationError` result explicitly
- [ ] Log every `request->send()` call site reached
- [ ] Log `uxTaskGetStackHighWaterMark()` for the AsyncTCP task per request —
      a stack overflow there is a live suspect and would explain silence
- [ ] Log free internal heap and largest free block per request

## 3. Identify the transition

- [ ] Establish what differs between a run that serves 24 consecutive body
      POSTs and one that fails from 9 s of uptime. Elapsed time alone has
      already been ruled out
- [ ] Determine whether the degraded state is per-connection, per-request, or
      global to the server
- [ ] Leave a monitor attached during ordinary use until the fault recurs. It is
      not reproducible on demand, so waiting for it with logging in place beats
      guessing at a trigger
- [ ] Confirm or eliminate the sharpest contradiction in the current evidence:
      while degraded, a malformed-JSON body should short-circuit to
      `400 Invalid JSON` without touching NVS, and it does not. The failing span
      contains only `JsonDocument doc;` and `deserializeJson()`
- [ ] Check whether concurrent activity is implicated — `/api/status` calls
      `loadDeviceConfig()` on **every** poll, opening and closing NVS each time,
      which is heavy and worth questioning on its own merits

## 4. Fix

- [ ] Fix the root cause, or adopt a handler shape that avoids it
- [ ] If the handlers are reshaped, do it across all nine body-POST routes in
      one pass, with the reason recorded in `design.md` — not as a guess
      (see the proposal's non-goals)
- [ ] Add a regression check that would catch a silent reappearance
- [ ] Verify the fix survives an extended soak, not just the first minute after
      boot, since "works right after a reboot" is the pattern this bug already
      shows

## 5. Unblock the control UI

- [ ] Re-run the `add-web-control-ui` hardware verification list in full
- [ ] Archive `add-web-control-ui`

## Evidence already gathered (2026-09-02, unit F32EB0)

Recorded so it does not have to be rediscovered:

- Sticky: never recovers without a reboot
- The first body POST after a restart has succeeded on every attempt
- Not uptime alone: failures from 9 s; 24 consecutive successes minutes in
- Not heap: ~50 KB free degraded vs ~52 KB fresh, `largest_free_block` unchanged
- Not routing: no duplicate or wildcard routes; no middleware registered
- The body callback **is** entered while degraded — CSRF still returns 403
- Affects untouched routes (`/api/settings/elevation`), so it is not new
- No-body POSTs and GETs are unaffected throughout
- `send()` deletes any existing response, so an unguarded `onRequest` diagnostic
  reports a false negative — one probe was already invalidated this way
- Library is ESP Async WebServer 3.12.0; 501 originates at `WebRequest.cpp:1044`

Ruled out **after** the initial write-up, once logging was working:

- Not uptime — 60 consecutive body POSTs succeeded at ~450 s uptime
- Not "a no-body POST poisons later body POSTs" — interleaved, all succeed
- Not USB CDC TX blocking with no host draining — succeeds with no monitor
- Not concurrent `/api/status` polling — succeeds under concurrent load
- **Not currently reproducible at all**: 80+ consecutive body POSTs succeed.
  The handler logs `Target temperature set to ... C` on every one, so it is
  running end to end
