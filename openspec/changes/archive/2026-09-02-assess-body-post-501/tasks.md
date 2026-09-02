# Tasks: Diagnose the body-POST 501

Ordered. Step 1 gates everything else — without logs the rest is guesswork.

## 1-5. Original investigation plan — OBSOLETE

These steps planned an investigation into a firmware fault that did not exist.
They are left unticked deliberately: ticking them would claim work that was
never needed, and deleting them would hide how the investigation was framed.

The plan was sound for the hypothesis it was built on — instrument the request
lifecycle, find the transition into the degraded state, fix it. The hypothesis
was wrong, and the one step that did get built (the RAM ring buffer, added
because serial could not observe the failing case) is what disproved it.

Superseded by the resolution below.

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

## 2026-09-02: a reproducible A/B, and one hypothesis disproved

While building the Shelly actuator the fault recurred, and this time it
reproduces on demand.

```
serial monitor DETACHED    body POST     501 x 8 (every time)
                           no-body POST  200 x 3
serial monitor ATTACHED    body POST     200 x 3
```

Attaching `pio device monitor` makes body-carrying POSTs work; detaching it
makes them fail. Same firmware, same uptime range, back to back. This is the
first reliable handle on the fault in the whole investigation.

**Disproved: USB CDC transmit blocking.** The obvious mechanism was that
`Log.h` writes every log line to `Serial`, and with no host draining the CDC a
write blocks the calling task — including handlers on the AsyncTCP task. Adding
`Serial.setTxTimeoutMs(0)` in `setup()`, which makes an undrained write drop
rather than block, **did not fix it**: body POSTs still returned 501 with the
monitor detached. The change was reverted rather than shipped unexplained.

So the monitor's presence matters, but not through transmit blocking. The next
candidate is whatever else keys off the CDC being "connected" — DTR assertion is
the obvious one, since that is what a monitor changes and what a bare port open
does not.

**Correction to the earlier evidence.** Some 501s recorded on 2026-09-02 were a
*different* bug: `server.on("/api/actuator")` uses `Type::BackwardCompatible`
matching (`WebServer.cpp:335`), which also swallows `/api/actuator/recheck`, so
a no-body POST reached a body-handler route and returned 501. That was route
shadowing, fixed with `AsyncURIMatcher::exact`, and is unrelated to this fault.
The original evidence stands: this affects body-carrying POSTs on routes with no
prefix collision at all, such as `/api/settings/elevation`.

### Correction: the monitor A/B does not hold either

Tested directly rather than by inference:

```
port closed entirely            body POST  200 x 5
port open, DTR de-asserted      body POST  200 x 5
port open, DTR asserted         body POST  200 x 5
```

"Port closed entirely" is the exact condition that had produced 501 x 8 minutes
earlier. So the serial monitor is **not** the variable, the DTR hypothesis is
dead alongside the transmit-blocking one, and the earlier claim of a
reproducible A/B was coincidence read as causation.

What the two observations share is that each success followed a reflash, i.e. a
recent boot — which is the *original* hypothesis, discarded earlier on the
strength of one run that survived several minutes. It deserves re-testing
properly, with the transition timed rather than sampled.

- [ ] Re-test the uptime hypothesis with discipline: from a cold boot, issue one
      body POST per minute and record the exact uptime at which the first 501
      appears, repeated across several boots. Sampling opportunistically is what
      produced two false leads already
- [ ] **The failing case cannot be observed over serial**, because reading the
      log requires attaching a monitor and the fault has so far never been seen
      while one was attached. A diagnostic that survives this needs to record
      into a RAM ring buffer readable over a GET — GETs are unaffected — rather
      than relying on a log channel that may itself perturb the fault

## RESOLVED 2026-09-02: the fault was in the test commands, not the firmware

The RAM ring buffer found it in one reading:

```
FAILING   code=-1  stages=none  len=31  params=1
          ctype='application/x-www-form-urlencod'
WORKING   code=200 stages=body|csrf|json|valid|sent  params=0
          ctype='application/json'
```

`stages=none` proved the body callback was never entered, and `ctype` gave the
reason. `curl -v` confirmed it on the wire: the requests were going out with
**`Content-Type: application/x-www-form-urlencoded`** and no CSRF header.

The cause was quoting. The diagnostic commands built headers into a shell
variable and expanded it unquoted:

```sh
CH='-H Content-Type:application/json -H X-Requested-With:KlimaControl'
curl -X POST $CH -d '{...}' "$URL"      # zsh does not word-split $CH
```

This session's shell is **zsh**, where unquoted parameter expansion does not
word-split as it does in bash. The whole string arrived as one meaningless
argument and both headers were silently dropped, so curl fell back to its
default content type for `-d`.

The firmware then did exactly what ESPAsyncWebServer is designed to do: a
form-urlencoded body sets `_isPlainPost`, the body is parsed into request
parameters, `handleBody` is never called, no response is produced, and `_send()`
substitutes `501 Handler did not handle the request`.

With correct quoting: 10/10 body POSTs succeed, and `/api/settings/elevation`,
`/api/settings/device-name` and `/api/temperature/target` — the three routes
originally reported as broken — all return 200.

### Every earlier hypothesis, and why each looked plausible

| hypothesis | why it seemed to fit | actual reason |
|---|---|---|
| uptime / degradation | successes clustered after reflashes | post-flash checks happened to use correctly quoted commands |
| USB CDC transmit blocking | monitor attached seemed to fix it | the monitor-attached runs used a different, correctly quoted helper |
| DTR assertion | same correlation | same coincidence |
| heap or fragmentation | plausible for an ESP32 | heap was ~48 KB throughout, entirely healthy |
| no-body POSTs immune | genuinely true | they carry no Content-Type, so `_isPlainPost` never triggers |

The invariant nobody spotted: every "working" observation came from a helper
that passed headers as separate arguments, and every "failing" one from the
unquoted variable.

- [x] Root cause identified and confirmed on the wire
- [x] Ring-buffer diagnostic built (`Support::RequestDiag`, 10 native tests,
      `GET /api/diag/requests`) — it is what closed this out, and it stays
- [x] Archive this change

## The one real firmware improvement this suggests

`501 Handler did not handle the request` is a terrible answer to "you sent the
wrong Content-Type". It cost hours here and would cost any future caller the
same. A JSON route receiving a form-urlencoded body should answer **415
Unsupported Media Type** naming the expected type.

- [ ] Reject non-JSON content types on JSON routes with 415 and a clear message,
      rather than letting the framework fall through to 501. **Carried forward
      as a spec requirement** (`http-api`, "Unsupported request content types
      are rejected explicitly") rather than as an open task here — it is real
      work, but it belongs to whichever change next touches the route handlers
