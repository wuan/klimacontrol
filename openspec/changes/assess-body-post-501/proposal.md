# Diagnose body-carrying POST endpoints returning 501

## Why

Every HTTP POST that carries a request body intermittently returns
`501 Handler did not handle the request` and has no effect. Once a device enters
this state it stays there until reboot. GETs and body-less POSTs keep working
throughout.

Observed on the `Test` unit (`F32EB0`) on 2026-09-02 while verifying
`add-web-control-ui`:

| route | body | result when degraded |
|---|---|---|
| `POST /api/settings/device-name` | yes | 501 |
| `POST /api/settings/elevation` | yes | 501 |
| `POST /api/syslog` | yes | 501 |
| `POST /api/temperature/target` | yes | 501 |
| `POST /api/control/enable` | no | 200, works |
| `POST /api/control/disable` | no | 200, works |
| `GET /api/status`, `GET /api/about` | – | 200, works |

This is pre-existing and unrelated to any recent change — it hits routes such as
`/api/settings/elevation` that have not been touched. It went unnoticed because
until `add-web-control-ui` added a stepper, almost nothing in the shipped UI
exercised a body-carrying POST on a running device.

**It blocks `add-web-control-ui` from being archived.** The setpoint stepper
posts to `/api/temperature/target`, so half that feature cannot work on
hardware. The enable/disable toggle is unaffected.

## What is already known

Established by black-box probing, all of it reproducible:

- **Sticky.** Once failing, it never recovers; a reboot always clears it.
- **Works immediately after boot.** The first body POST after a restart has
  succeeded every time it was tried. Later ones fail.
- **Not uptime alone.** One run failed from 9 s of uptime onward; another
  succeeded 24 consecutive body POSTs several minutes in. Something other than
  elapsed time flips it.
- **Not heap exhaustion.** Free heap is ~50 KB degraded vs ~52 KB fresh;
  `largest_free_block` is unchanged. `min_free_heap` dipped to 41 808 B.
- **Not a route-registration problem.** No duplicate or wildcard routes; no
  middleware is registered (`AccessLogger` is declared in `WebServerManager.h`
  but never added to the server).
- **The body handler is entered.** A request missing the CSRF header still
  returns 403 while degraded, and that check lives inside the body callback.
  Everything *after* it appears not to respond.
- **501 means no response object.** `AsyncWebServerRequest::_send()`
  (`WebRequest.cpp:1044`) emits it when `_response` is null at send time, i.e.
  neither the body callback nor the request callback produced a response.
- **`send()` replaces silently.** `AsyncWebServerRequest::send()`
  (`WebRequest.cpp:1251`) deletes any existing response before storing the new
  one, so a diagnostic that responds unconditionally from `onRequest` destroys
  the body handler's real answer. One probe here was invalidated that way; a
  non-destructive probe must test `getResponse() == nullptr` first.

Library: ESP Async WebServer 3.12.0.

## Diagnostic tooling: what actually works

An earlier revision of this proposal claimed the device had no working log
channel. That was wrong, and the error was in the method, not the device.

`pio device monitor -e adafruit_qtpy_esp32s2` works and shows logs. The failed
attempts used raw pyserial against the port with DTR/RTS at their defaults and
toggled RTS to force a reset, which on a native-USB-CDC part re-enumerates the
port and drops the handle. Use the PlatformIO monitor with the environment.

What is genuinely missing is narrower, and still matters here:

- **Requests are not logged.** `AccessLogger` is defined in
  `WebServerManager.cpp:24` and held as a member (`WebServerManager.h:64`) but
  is **never registered** with the server — nothing calls `addMiddleware()`.
  It logs method, URL, elapsed time and response code, and would have shown
  every 501 with its status directly. Registering it is the single highest-value
  diagnostic step available.
- **`ESP_LOGD` is compiled out.** `CORE_DEBUG_LEVEL` defaults to `0`
  (`Log.h:47`), so debug-level diagnostics do not exist in a normal build. A
  diagnostic build needs `-DCORE_DEBUG_LEVEL=4`.
- **The monitor drops on reboot.** Native USB CDC re-enumerates when the device
  restarts, so a monitor attached before a reboot does not survive it and the
  boot log is missed. This is why `Reset reason:` has been hard to capture.

## Reproduction status

**Not currently reproducible.** The fault was observed repeatedly over roughly a
forty-minute window on 2026-09-02, across several firmware builds. It has since
stopped: more than eighty consecutive body-carrying POSTs now succeed, with and
without a monitor attached, at uptimes from seconds to eight minutes.

Hypotheses tested since and **not** supported:

| Hypothesis | Result |
|---|---|
| Degrades with uptime | Ruled out — 60 consecutive successes at ~450 s uptime |
| A no-body POST poisons later body POSTs | Ruled out — interleaved, all succeed |
| USB CDC TX blocking when no host drains it | Not supported — succeeds with no monitor attached |
| Concurrent `/api/status` polling | Not supported — succeeds under concurrent polling |

With logging confirmed working and the handler observed writing on every
success (`Target temperature set to 23.5 C` per request), the next step is to
register `AccessLogger` and leave a monitor running during ordinary use until
the fault recurs, rather than to keep guessing at a trigger.

## What Changes

- Register `AccessLogger`, so every request and its response code is logged.
- Capture a recurrence with logging in place and identify why `_response` is
  null for body-carrying POSTs in the degraded state.
- Fix the defect, or — if it is a library bug — adopt whichever handler shape
  avoids it and record why.
- Add a `http-api` requirement that a body-carrying POST always produces a
  response, and a `system-architecture` requirement that the device stays
  diagnosable.
- Re-run the `add-web-control-ui` hardware verification and archive it.

## Non-goals

- Rewriting existing handlers to a different shape before the cause is known.
  Every body-POST route in the codebase uses the same
  `server.on(uri, method, onRequest, nullptr, onBody)` pattern; changing all of
  them on a guess would be a large, unreviewable diff against an unproven
  theory.
- Upgrading or forking ESP Async WebServer as a first move.
- Any change to the control UI itself; it is complete and blocked only by this.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `http-api`: a requirement that every request matched by a handler produces a
  response, with body-carrying POSTs called out explicitly.
- `system-architecture`: a requirement that the firmware retains at least one
  working log channel and that debug-level diagnostics are reachable.

## Impact

- **Source**: unknown until diagnosed. Likely `src/routes/*.cpp`,
  `src/WebServerManager.cpp`, possibly `Log.h` / `platformio.ini` for logging.
- **Blocks**: `add-web-control-ui` (verified and otherwise complete).
- **Risk**: this affects every settings-writing endpoint on the device, not just
  the new ones. Any user changing MQTT, syslog, elevation, timezone or device
  name on a device that has been up for a while is likely hitting it silently.
