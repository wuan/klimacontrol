# Design: Diagnosing the body-POST 501

## Where the 501 comes from

```
ESPAsyncWebServer 3.12.0, WebRequest.cpp

  _onData()  [PARSE_REQ_BODY]
      │
      ├─ content-type is JSON, so _isPlainPost stays false
      │     └─► _handler->handleBody(...)   ──► our onBody lambda
      │                                            ├─ verifyCsrfHeader()  → may send 403
      │                                            ├─ deserializeJson()   → may send 400
      │                                            ├─ range check         → may send 400
      │                                            └─ send(200)
      │
      └─ when _parsedLength == _contentLength:
             _runMiddlewareChain()  ──► _handler->handleRequest()  ──► our empty onRequest
             _send()
                 └─ if (!_response) send(501, "Handler did not handle the request")
```

So a 501 means `_response` was null after both callbacks ran. Either the body
callback never reached a `send()`, or its response went missing.

Two library behaviours matter and are easy to trip over:

1. **`send()` destroys the previous response** (`WebRequest.cpp:1251`): it
   `delete`s `_response` before assigning the new one. A diagnostic that
   responds unconditionally from `onRequest` therefore *erases* the body
   handler's real answer and reports a false negative. This already invalidated
   one probe in the 2026-09-02 session. Any `onRequest` probe must be guarded:

   ```cpp
   if (request->getResponse() == nullptr) { /* only now report */ }
   ```

2. **`canHandle()` requires a non-null `_onRequest`** (`WebHandlers.cpp:302`).
   A route registered with only a body callback never matches at all, so the
   empty `onRequest` lambda in every route here is load-bearing, not vestigial.

## What the evidence rules in and out

| Hypothesis | Status |
|---|---|
| Route shadowing / duplicate registration | **Out** — no wildcards, no duplicate paths |
| A CSRF middleware short-circuiting | **Out** — `AccessLogger` is declared but never registered; no middleware exists |
| Heap exhaustion | **Out** — ~50 KB free while degraded, `largest_free_block` unchanged |
| Handler not entered at all | **Out** — CSRF still returns 403 from inside the body callback |
| Caused by a recent change | **Out** — untouched routes (`/api/settings/elevation`) fail identically |
| Uptime threshold | **Out** — failures seen from 9 s; successes seen minutes in |
| Something sticky in per-request or per-connection state | **Open** — best remaining fit: it never self-heals, and reboot always clears it |
| NVS/Preferences stalling the AsyncTCP task mid-handler | **Open** — would explain a missing `send()` on paths that write NVS, but *not* the 400 paths, which also fail while degraded |

The last row is the sharpest contradiction in the data and the best place to
aim: while degraded, a malformed-JSON request should short-circuit to
`400 Invalid JSON` without touching config or NVS, and it does not. Whatever is
wrong sits between entering the body callback and the earliest `send()` after
the CSRF check — a span containing only `JsonDocument doc;` and
`deserializeJson()`.

That points at ArduinoJson allocation inside the AsyncTCP task as the prime
suspect, but "allocation failed" should surface as `400 Invalid JSON` via the
`DeserializationError` path, not as silence. A hard fault or a stack overflow in
the AsyncTCP task would explain silence — and the AsyncTCP task stack is a
plausible culprit given `JsonDocument` and the lambda frames sit on it — except
that the device demonstrably keeps serving other requests afterwards.

Resolving that requires seeing what the task actually does, which is why
logging comes first.

## Sequencing

```
  1. Get a log channel back
        │   without this every step below is guesswork
        ▼
  2. Instrument the request lifecycle
        │   entry/exit of onBody, deserialize result, each send() call,
        │   AsyncTCP task high-water mark
        ▼
  3. Identify the transition into the degraded state
        │   what is true at 9 s in one run and not in another
        ▼
  4. Fix, or adopt a handler shape that avoids it — with the reason recorded
        ▼
  5. Re-run add-web-control-ui verification and archive it
```

### Step 1 in detail

`pio device monitor -e adafruit_qtpy_esp32s2` works. An earlier revision of this
document claimed otherwise; that was a method error — raw pyserial with default
DTR/RTS, plus an RTS toggle that re-enumerates a native-USB-CDC port and drops
the handle.

Three real gaps remain, in order of value:

| Gap | Why it matters | Fix |
|---|---|---|
| `AccessLogger` never registered | no per-request log line, so a 501 leaves no trace at all | call `addMiddleware(&logging)`; it already formats method, URL, elapsed ms and response code |
| `ESP_LOGD` compiled out | `CORE_DEBUG_LEVEL` defaults to 0, so debug diagnostics do not exist in a normal build | a `debug` env with `-DCORE_DEBUG_LEVEL=4` |
| Monitor drops on reboot | native USB CDC re-enumerates, so the boot log and `Reset reason:` are missed | reattach after reset, or capture over a second channel for boot-time work |

Registering `AccessLogger` is the highest-value step: it turns an invisible
failure into a logged one. Note it reads `request->getResponse()` and logs
`(no response)` when there is none — precisely the 501 case — so it
distinguishes "handler produced nothing" from "handler produced a 501" without
any extra instrumentation.

## Why not just rewrite the handlers now

The tempting move is to switch every body-POST route to buffer the body and do
the work in `onRequest`, or to adopt `AsyncCallbackJsonWebHandler`. That may
well be the eventual fix. It is deliberately not the first step because:

- it is a large diff across nine routes, justified by a theory that has not been
  tested;
- if the real cause is a task-stack overflow or an allocation fault, moving the
  same work to a different callback on the same task changes nothing;
- and with no logs, there would be no way to tell a real fix from the bug simply
  becoming rarer — which, given how timing-dependent this already looks, is the
  most likely outcome of a blind change.
