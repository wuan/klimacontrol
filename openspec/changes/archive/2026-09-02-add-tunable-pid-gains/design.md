## Context

`PidController` is already built for this change. It takes `PidGains` by
constructor, exposes `getGains()`, and `setGains()` already replaces the gains
and suspends — with the right rationale attached (an integral accumulated under
the old gains means something else under the new ones). Nothing in the algorithm
needs to change.

What is missing is everything around it: no config fields, so no persistence, no
validation, no API, and no path from a stored value into the running controller.
`acceptAutotuneResult()` bridges the gap in memory only and says so in a comment.

Three constraints shape the design:

- **`updateControl()` now does three jobs**, not one. It evaluates the
  over-temperature shutoff, ticks the autotuner when a run is active, and
  computes the PID. They want different cadences.
- **`PidController` is documented as single-writer** — owned by the Sensor
  Monitor task. That invariant is currently violated (see D4).
- **NVS keys are hard-limited to 15 characters** and fail *silently* when
  exceeded, which is why `Config.h` has `static_assert(nvsKeyFits(...))` per key.

## Goals / Non-Goals

**Goals:**

- Gains and the control interval stored in `DeviceConfig`, persisted to NVS,
  validated on load like every other field.
- An accepted autotune result survives a restart.
- The PID computation runs on a cadence appropriate to the plant, without
  slowing the safety shutoff or the autotuner's sampling.
- Defaults consistent with the tuning method the firmware already ships.

**Non-Goals:**

- **Auto-applying an autotune result.** Acceptance stays explicit. A run that
  silently rewrote the gains of a heating system overnight is a poor default,
  and the numbers are worth a human glance given what they cost to obtain.
- **Gain scheduling, or per-season profiles.** One set of gains.
- **Re-tuning the autotuner's derivation.** Tyreus–Luyben and `Kd = 0` are
  already specified and already implemented.
- **Making the control interval adaptive.** A fixed configurable period.
- **A migration path for stored gains.** There are none to migrate; no device
  has ever persisted a gain.

## Decisions

### D1 — Decimate inside `updateControl()`, not in the caller

The earlier sketch of this work put a decimation counter in `SensorMonitor` and
called `updateControl()` every Nth tick. That is now wrong, because two of the
three things `updateControl()` does must not be slowed:

| job | cadence | why |
|---|---|---|
| over-temperature shutoff | sensor tick | a safety limit noticed up to 60 s late is a regression against a requirement that exists to stop a valve |
| `autotuner.update()` | sensor tick | it samples an oscillation to measure it; see D2 |
| `pid.update()` | control interval | the only part whose cost is noise |

So the guard goes around the `pid.update()` call alone. `updateControl()` keeps
being called on every sensor tick, which is also what keeps `pid.suspend()`
marking skipped ticks — decimating the *call* would reintroduce the
stale-timestamp bug `add-web-control-ui` fixed, where the next computing tick
sees a `dt` spanning the whole gap and saturates the integral on the spot.

*Alternative considered:* split `updateControl()` into `updateSafety()` and
`updateControl()` and let `SensorMonitor` call them at different rates. Rejected
— it moves loop policy into the task and gives the web task two more entry
points into control state, for no gain over a guard in one place.

### D2 — The autotuner keeps the sensor tick, deliberately

This is the subtle one. The autotuner inverts the amplitude of an induced
oscillation:

```
  Ku = 4d / (π · √(a² − h²))          a = half peak-to-peak amplitude
```

`a` appears in a denominator. Sampling at 60 s can miss the extremum of a peak,
which under-estimates `a`, which **over**-estimates `Ku`, which produces gains
more aggressive than the plant can take — from a run that reports convergence.
The failure is silent and it lands in the one component whose entire purpose is
measurement.

Underfloor `Tu` is hours, so 60 s sampling would be adequate for the *period*.
It is the amplitude that does not tolerate it. Keeping the autotuner at 1 s costs
nothing: it only runs during a run.

### D3 — Time-based decimation, not tick counting

Compute when `now - lastPidComputeMs >= control_interval_s * 1000`, with unsigned
arithmetic so it is correct across the `millis()` rollover — the same pattern
`PidController::update()` and `RelayAutotuner` already use.

Counting every Nth tick would couple the control interval to the sensor reading
interval, which is itself settable (`SensorMonitor::setReadingInterval()`).
Changing the sensor cadence would then silently change the control cadence, and
the configured "60 seconds" would mean whatever 60 ticks happened to be.

Note the interval is a *floor*, not a schedule: a tick that arrives late runs
late, and `dt` is measured, not assumed.

### D4 — Route gain changes through the control task

`acceptAutotuneResult()` is called from the AsyncTCP web task
(`ControlRoutes.cpp:408`) and calls `pid.setGains()`, which writes `gains` and
`running`. The Sensor Monitor task is concurrently in `pid.update()`, which reads
`gains` and read-modify-writes `running`, `integral` and `lastComputeMs`. That
contradicts `PidController.h:60-62`, which states the web task never touches PID
state and concludes there is no race to guard against.

It is a pre-existing latent race, shipped with `wire-autotune-into-device`, and
it is not merely theoretical:

- `running = false` can be **lost**. If `update()` is mid-call, it writes
  `running = true` on the way out, after `setGains` wrote `false`. The suspend
  is dropped and the integral accumulated under the old gains carries into the
  new ones — precisely what `setGains()` exists to prevent.
- `PidGains` is three floats. A tick can read a mix of old and new.

This change touches exactly this path, so it fixes it rather than extending it.
Gain changes become a deferred request consumed on the control task, reusing the
pattern `autotuneStartRequested` / `autotuneCancelRequested` already establish:
the web task sets an atomic flag, the control tick picks it up and calls
`setGains()` itself. `PidController` stays genuinely single-writer and its
comment becomes true again.

*Alternative considered:* a mutex around PID state. Rejected — the codebase's
existing answer for web-task-to-control-task signalling is an atomic request
flag, and a mutex on the 1 s control path for a change that happens twice a year
is the wrong trade.

### D5 — Validation catches typos, not bad tuning

A range check cannot tell good tuning from bad; it can only catch a misplaced
decimal point. So the bounds are generous and their job is stated as such:

| field | range | default | reasoning |
|---|---|---|---|
| `kp` | `[0.01, 100]` | `0.5` | at `0.5`, a 2 K error saturates the output |
| `ki` | `[0, 0.05]` | `0.0001` | at `1e-4`, a 1 K error for an hour accumulates `0.36` |
| `kd` | `[0, 600]` | `0` | zero is the autotuner's own answer and the new default |
| `control_interval_s` | `[1, 600]` | `60` | 1 preserves today's behaviour for anyone who wants it |

`ki = 0` is permitted: a P-only controller is a legitimate, if droopy, choice.
`kp = 0` is not, because it disables control while reporting it as enabled.

The shipped `Ki = 0.1` sits above the new maximum. That is the point — it
saturates the integral in ten seconds at 1 K of error, and no plausible
underfloor tuning is within two orders of magnitude of it. Rather than widen the
range to admit a value nobody would choose, the default changes and the old
value becomes unrepresentable.

Per the convention `add-web-control-ui` set, the **API rejects rather than
clamps**, while **load-time validation falls back to the default** — a corrupt
NVS read must not refuse to boot, but a user's typo must not be silently
rewritten into something they did not ask for.

*Alternative considered:* deriving the bounds from the autotuner's plausible
output range. Attractive, but it inverts the dependency — the autotuner's output
would then be definitionally in range and the check would prove nothing about a
hand-entered value.

### D6 — Four separate NVS keys, gains written as a set

Keys follow the `ConfigManager` private-constant convention (`Config.h:258+`)
with a `nvsKeyFits()` `static_assert` each, not the older `PrefsKeys.h` header:
`"pid_kp"`, `"pid_ki"`, `"pid_kd"`, `"ctrl_intv"`. All well inside 15
characters; `"ctrl_intv"` is abbreviated for the same reason `"disp_intv"` is.

The write method is `updateTuning(kp, ki, kd, intervalS)`, taking all four
together in the shape of `updateActuatorTiming()`. Gains are only meaningful as
a set: accepting three of four and reverting one leaves a controller nobody
configured.

*Alternative considered:* one packed struct in a single NVS blob. Rejected — no
other field group in this codebase does that, and it forfeits per-field
fallback on a partial corruption.

### D7 — Persisting acceptance reuses the same path

`acceptAutotuneResult()` gains a `config.updateTuning(...)` call. Because of D4
it now runs on the control task, where an NVS commit of tens of milliseconds is
comfortably inside the 1 s watchdog cadence. Ordering is persist-then-apply: if
the write fails, the gains the user sees are the ones that will survive.

`Kd` is written as the autotuner produced it, which is `0`.

## Risks / Trade-offs

- **Decimating the wrong thing.** The whole change hinges on the guard sitting
  around `pid.update()` and nothing else. → Tests assert that the safety
  shutoff engages within one sensor tick of the limit being crossed, and that
  `autotuner.update()` is called on every tick during a run, both with a 60 s
  control interval configured.
- **Fixing the D4 race changes acceptance from synchronous to deferred.** `POST
  /api/autotune/accept` currently reports the outcome in its response; it can no
  longer know whether the control task applied the gains. → The endpoint reports
  that the request was *accepted* and validated, and `GET /api/control` reports
  the gains actually in force — which must therefore read from the running
  controller, not from config, or it would report a pending change as applied.
  Reading it back is the confirmation, and is what makes the change observable
  in the UI.
- **New defaults change behaviour on upgrade** for any device with no stored
  gains — which is all of them. → Intended, stated as breaking in the proposal.
  The old defaults are not a tuning anyone chose, and no device has run a
  heating season on them. A device mid-experiment is not a concern in September.
- **A user can now tune the device into instability**, and the output drives a
  real valve. → Bounded by the over-temperature shutoff, which is independent of
  the gains and evaluated before the PID. The settings UI states that these
  values drive a physical valve.
- **`Ki` legibility.** A sensible value is `0.0001`, which is easy to mistype by
  an order of magnitude and hard to eyeball in a form field. → The UI shows the
  field with its unit and range, and shows the resulting integral time
  `Ti = Kp / Ki` in seconds alongside it, which is the number with physical
  meaning and the one Tyreus–Luyben actually derives.

## Migration Plan

No data migration: no gain has ever been persisted, so every device takes the
new defaults on first boot after the upgrade and stores them on the first write.

Rollback is a firmware downgrade. Stored gains would then be ignored rather than
misread — the older firmware never reads those keys — so a downgrade returns the
old compiled-in defaults and leaves the stored values intact for a re-upgrade.

Order of work: config fields and validation first (testable in `native` with no
device), then the decimation and the D4 fix, then persistence of acceptance,
then the API, then the UI. Each step is independently testable and the change is
useful after the third.

## Open Questions

- **Are the proposed defaults right?** They are derived from the arithmetic in
  D5, not from a run on the actual plant. The honest answer is that nobody knows
  until a zone is delivering heat and an autotune converges — which is what this
  change exists to make durable. The defaults only need to be safe and
  non-pathological, not optimal, and they should be revisited after the first
  converged run rather than defended.
- **Should `control_interval_s` be exposed in the UI at all**, or fixed at 60 s
  in config and left to the API? It is a field with no good reason to be changed
  by hand, and a wrong value degrades control quietly. Leaning towards exposing
  it under the same "these drive a valve" warning as the gains, on the grounds
  that hiding a persisted field makes it harder to diagnose.
- **Does the derivative term earn its keep at all now?** `Kd = 0` is the new
  default, the autotuner always produces zero, and the spec says derivative
  action is harmful on this plant. Keeping the term is nearly free and removing
  it is a spec change to `temperature-control` §"PID algorithm", so it stays —
  but if it is still zero everywhere in a year, it is dead weight.