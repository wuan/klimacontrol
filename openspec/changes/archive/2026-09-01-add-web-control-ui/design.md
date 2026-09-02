# Design: Web control UI and bumpless PID restart

## Overview

Two loosely coupled pieces ship together because the first makes the second
reachable by ordinary use:

1. A stepper and a toggle on the dashboard, wired to endpoints that already
   exist. Almost entirely `data/control.html`.
2. A bumpless restart of the PID state, so that toggling control on does not
   produce a saturated first output.

```
┌──────────────────────────────────────────────────────────────┐
│  ALREADY BUILT                                               │
│                                                              │
│   Config/NVS ──► SensorController ──► PID ──► output         │
│       ▲                  │                                   │
│       │                  ├──► /api/status ──────┐            │
│   ControlRoutes ◄────────┘                      │            │
│       ▲                                         ▼            │
│       │  POST target / enable / disable    control.html      │
│       │                                    (read-only)       │
│  ═════╪═════════════════════════════════════════╪══════════  │
│       └────────────── THIS CHANGE ──────────────┘            │
│              stepper + toggle POST back                      │
└──────────────────────────────────────────────────────────────┘
```

## Decision 1: Stepper on the control page, not Settings

A setpoint is touched daily; MQTT credentials are not. It belongs with the live
readings, in the `.control-bar` restructured by #36, not behind a navigation
hop. The existing `.control-item` for **Target** becomes interactive in place.

```
        before                          after
┌──────────────────────┐      ┌──────────────────────────┐
│  Target  │  Control  │      │  Target      │  Control  │
│  21.5°C  │     ●     │      │ ⊖ 21.5° ⊕    │  [==●] ●  │
└──────────────────────┘      └──────────────────────────┘
                                 stepper       toggle + symbol
```

The existing three-symbol state indicator (`−` / `○` / `●`, red / purple /
green) is **kept** — it carries information the toggle cannot. The toggle says
what the user asked for; the symbol says what the controller is doing. A device
that is enabled but not currently driving output (`○`) is exactly the case where
those two differ and where the distinction matters.

Rejected alternatives: a slider (poor fit for 0.5 °C granularity on a phone, and
the range is only 40 steps wide), and a number input with an Apply button (adds
a commit step and a mobile keyboard for what should be a thumb tap).

## Decision 2: Reject out-of-range setpoints instead of clamping

`http-api` §"Out-of-range setpoint" already requires HTTP 4xx with the setpoint
unchanged. The implementation clamps and reports success.

Given a client-side clamped stepper, an out-of-range value can now only arrive
from a hand-rolled request or a buggy client — precisely the callers that
benefit from being told. Clamping-and-echoing was considered (return the applied
value in the response body) but it contradicts the written requirement and
leaves `{"success":true}` meaning "we did something other than what you asked".

There is a second validator to reconcile. Two functions disagree about what an
invalid setpoint means:

| Function | Out-of-range behaviour |
|---|---|
| `SensorController::setTargetTemperature()` | clamps to `[10, 30]` |
| `Config::updateTargetTemperature()` | resets to `22.0` |

The second is currently unreachable through the HTTP path because the first
clamps before calling it. Moving validation up into the route handler makes
`setTargetTemperature()`'s clamp the last line of defence for non-HTTP callers
(`main.cpp:272` restores from NVS at boot) and leaves `Config`'s reset as the
guard against a corrupt NVS value — which is the case it was written for. Note
that the clamp also maps `NAN` to `30.0`, since `std::min(30.0f, NAN)` returns
`30.0f`; the route's `is<float>()` check keeps NAN out of the HTTP path, but the
range check in the handler should be written to reject NAN explicitly rather
than rely on that.

## Decision 3: Bumpless restart detected inside the loop, not in the setter

The obvious implementation — have `setControlEnabled()` zero the PID state — is
wrong on two counts.

**It races.** `setControlEnabled()` runs on the AsyncTCP web task;
`updateControl()` runs on the SensorMonitor task (`SensorMonitor.cpp:68`).

```
SensorMonitor:  read integral ─────────────► integral += Ki*e*dt ──► store
Web task:              └─ setControlEnabled: integral = 0 ─┘
                                                  ▲
                                    reset silently overwritten
```

Single-core ESP32-S2 means the 32-bit stores will not tear, but the
read-modify-write can straddle the reset and lose it.

**It is too narrow.** The early return at `SensorController.cpp:444` fires for
three distinct reasons, and all three leave `lastControlTime` stale:

```
     control off ──► on        sensor lost ──► back        boot ──► first tick
          │                          │                        │
          └──────────► "the loop did not run last tick" ◄──────┘
                                     │
                       reset integral + previousError,
                       reseat lastControlTime = now
```

So: `updateControl()` detects its own resumption. A `bool controlWasRunning`
member is set `false` on every early-return path and `true` at the end of a
completed computation; when a tick finds it `false`, it reseats the state before
computing. Only SensorMonitor ever touches PID state, so this is race-free by
construction and needs no mutex. `setControlEnabled()` stays a pure config
write.

The three `static`s become instance members as a consequence. That
independently fixes their being shared across all `SensorController` instances —
a real hazard in the native tests, where state leaks between cases.

### What a resumed tick computes

With `lastControlTime` reseated to `now`, the first tick after resumption has
`dt == 0`. The existing code already guards this: the derivative branch is
`if (dt > 0.0f)` and the integral increment `Ki * error * 0` is zero. So the
first output is pure proportional — the correct bumpless behaviour, and it needs
no new special case.

## Decision 4: Poll/edit arbitration

`setInterval(updateStatus, 10000)` writes `targetTemp` from the response
unconditionally. Tap `+` three times and an in-flight poll can render a stale
value mid-adjustment.

The chosen approach is a **pending-edit guard**: taps mutate a local desired
value and render it immediately; a short debounce (~400 ms) coalesces a burst
into one POST; while an edit is pending or a POST is in flight, `updateStatus()`
skips writing the setpoint field only — every other field still updates. The
guard clears once the POST resolves.

```
  tap + tap + tap ──► local 22.0 → 22.5 → 23.0  (rendered instantly)
                              │
                     400 ms quiet
                              ▼
                    one POST {"value": 23.0}
                              │
                       ◄─ 200 ─┤  guard clears, polls resume owning the field
```

This also collapses a burst of taps into a single NVS write. Flash wear was
never realistically a concern at human tap rates, but one write per gesture is
the better shape regardless.

The toggle is a single discrete action, so it needs no debounce — just the same
in-flight guard so a poll cannot flip the switch back under the user's finger.

## Open question: mirror the PID test, or extract it

`test/test_temperature_control/test_temperature_control.cpp:204` contains a
hand-copied reimplementation of `updateControl()` that takes `nowMs` as a
parameter so it can run on the host without `millis()`. The real function is not
under test.

Two ways forward:

| | Keep mirroring | Extract a `PidController` |
|---|---|---|
| Effort now | minimal | moderate — new type, inject clock, rewire `SensorController` |
| Risk | mirror and original drift silently; this change edits the exact logic that is duplicated | touches the control path in a change that is otherwise mostly HTML |
| Payoff | none | bumpless restart becomes directly testable, as does anti-windup |

The bumpless-restart logic is the kind of state-machine behaviour that is
genuinely worth testing, and testing it via a mirror tests nothing. Recommend
extraction — but it is a scope decision, so `tasks.md` carries it as an explicit
branch rather than assuming it.