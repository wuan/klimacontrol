# Design: Actuator output and relay autotune for underfloor heating

## The plant

Everything below follows from what underfloor heating actually is, so it is
worth stating the physics before the software.

```
  valve opens ──► warm water enters the loop ──► screed absorbs heat
                                                 (hours of thermal mass)
                                                        │
                                                        ▼
                              floor surface warms ──► air warms ──► sensor
                                                        │
                     ┌──────────────────────────────────┘
                     └─ dead time L: 15–60 min before the sensor moves at all
                        time constant T: 2–8 h to approach steady state
```

Two consequences dominate every decision here:

1. **Lag-dominant, not delay-dominant.** `L/T` is small-ish, which is the
   regime where integral action does the work and derivative action does
   nothing but amplify noise.
2. **Overshoot is expensive.** Once the screed is charged there is no way to
   discharge it quickly. A controller that overshoots by 1 K leaves the room too
   warm for hours. Conservative tuning is not timidity, it is the correct answer.

## Stage 1: time-proportional output

The valve accepts open or closed. The PID produces `0.0…1.0`. The bridge is a
slow PWM:

```
  T_cycle = 15 min, duty = 0.30

  │◄─────────────── cycle n ──────────────►│◄────── cycle n+1 ──────
  ├──── OPEN 4.5 min ────┼── CLOSED 10.5 min ──┼──── OPEN ────┼── ...
  ▲                                            ▲
  duty is sampled here                         and again here
```

The duty is **latched at the start of each cycle**, not tracked continuously.
Chasing a moving PID output within a cycle would produce switching patterns that
no longer correspond to a duty ratio, and the whole point of TPO is that the
average heat delivered equals the duty.

### Cycle time versus actuator travel

Thermal wax actuators — the common NC type on a manifold — take 3–5 minutes to
open and about as long to close. That is not a delay before switching; it is a
partial-stroke period during which flow is somewhere between zero and full.

If `T_cycle` is 5 minutes and the duty is 0.3, the valve is commanded open for
90 seconds — less than its travel time. It never leaves its seat, delivers
almost nothing, and the actual heat/duty curve is wildly non-linear at the low
end. So:

```
  T_cycle >= ~4 x travel time     →   15–20 min for a wax actuator
  T_cycle << plant time constant  →   well under an hour
```

15 minutes is the default. It also caps valve cycling at 4 operations/hour,
which matters for wear on a part rated in tens of thousands of cycles.

### Minimum dwell

Duties near 0 or 1 would command on-times of seconds. Below a floor, the
command is snapped to fully off or fully on for the whole cycle:

```
  duty < MIN_DUTY (travel/T_cycle)      →  closed for the whole cycle
  duty > 1 - MIN_DUTY                   →  open for the whole cycle
```

This is not a rounding convenience — it is what keeps the valve from being asked
to perform a stroke it cannot complete.

### Failsafe

This is the first change that makes the device move something physical, so the
failure modes need naming:

| Failure | Required behaviour |
|---|---|
| Firmware crash / watchdog reset | valve closes (pin undriven ⇒ relay de-energised ⇒ NC valve shut) |
| Boot, before the loop starts | valve closed; the pin is driven LOW before anything else |
| Sensor data invalid | valve closed, and the PID suspends (already implemented) |
| Control disabled | valve closed |
| Setpoint reached | normal TPO behaviour, duty may be 0 |

The whole failsafe story rests on **active-high drive into a normally-closed
valve**: an undriven pin is a closed valve. That is a wiring contract as much as
a firmware one, and the wiring doc must state it. A relay module with inverted
(active-low) logic would silently invert the failsafe, which is the single most
important thing to get right in the hardware notes.

### Open question: which GPIO

The e-paper panel already claims GPIO 36, 35, 18, 9, 8 and 17
(`src/display/DisplayPins.h`). Remaining broken-out pads on the QT Py ESP32-S2
are the TX/RX pair, the SDA/SCL pair (distinct from the STEMMA QT bus the
sensors use), and MI/GPIO37 — the last of which `DisplayPins.h` documents as
being reclaimed as an input by `SPI.begin()`, so it is unusable for output.

The choice must avoid strapping pins, because their level during reset is not
under firmware control and a strapping pin driving a relay means the valve
state during boot is decided by a pull-up somewhere. **This needs to be settled
against the board variant file before stage 1 starts**, and it is a hardware
decision rather than a software one. Following the precedent set by
`DisplayPins.h`, the pin should be a compile-time constant with `static_assert`
guards against collision, not a runtime setting — a bad runtime value could
break the web UI needed to correct it, and here it could also hold a valve open.

## Stage 2: control cadence

Sensors read every second; the control loop should not. Running a PID at 1 Hz
against a plant with an hours-long time constant means the derivative term sees
almost pure sensor noise and the integral accumulates 3600 tiny increments an
hour where 60 would do.

`SensorMonitor` gains a decimation counter: read every tick, call
`updateControl()` every `control_interval_s` (default 60). `PidController`
already takes its clock as a parameter, so nothing inside it changes.

Note the interaction with the work just completed in `add-web-control-ui`:
`updateControl()` must still be called on non-control ticks so it can call
`pid.suspend()` when disabled. Decimation applies to the *computation*, not to
the gating. Getting this backwards reintroduces exactly the stale-timestamp bug
that change fixed.

## Stage 3: gains as configuration

`Kp`, `Ki`, `Kd`, `T_cycle`, the control interval and the safety limits move
into `DeviceConfig` and NVS. `PidController` already takes `PidGains` by
constructor, so it grows a setter and `SensorController` pushes config changes
into it.

One ordering hazard: changing gains mid-run changes the meaning of the
accumulated integral. The cleanest answer is to treat a gain change as a
discontinuity and `suspend()` the controller, reusing the bumpless-restart
machinery already built rather than inventing a second path.

Validation ranges must be real, not decorative — a `Ki` typo of three orders of
magnitude now holds a valve open. Every field gets a clamped range, and the API
rejects rather than clamps, following the convention `add-web-control-ui`
established.

## Stage 4: relay autotune

### Why relay, not step response

Two standard identification experiments were considered:

| | Relay (Åström–Hägglund) | Open-loop step + FOPDT fit |
|---|---|---|
| Needs a continuous actuator | no — bang-bang is native here | no, but wants a clean step |
| Stays near setpoint | yes, oscillates ±a around it | no, drives the room far off |
| Sensitive to load disturbance | moderate | high — a sunny afternoon ruins the fit |
| Duration | 3–5 limit cycles | one settling time |
| Extra capability needed | none | none |

Relay wins on the one that matters most for a device in someone's living room:
it keeps the temperature near setpoint throughout instead of parking it 3 K away
for hours, and it is far more robust to the disturbances a real house produces.

### The experiment

```
  T ┤      ╭──╮        ╭──╮        ╭──╮        a  = half peak-to-peak of PV
    ┤     ╱    ╲      ╱    ╲      ╱    ╲       Tu = period between like peaks
  SP├────╱──────╲────╱──────╲────╱──────╲──    d  = relay half-amplitude
    ┤   ╱        ╲  ╱        ╲  ╱        ╲     h  = switching hysteresis
    ┤  ╯          ╲╯          ╲╯          ╲
    └──────────────────────────────────────
 out ▄▄▄▄____▄▄▄▄____▄▄▄▄____▄▄▄▄____
```

Output switches fully on when the temperature falls below `SP − h`, fully off
when it rises above `SP + h`. The hysteresis band is what stops sensor noise
from producing spurious switches; it must be several times the sensor's
resolution.

Ultimate gain, with the hysteresis correction (the uncorrected `4d/(πa)` form
over-estimates `Ku` and yields tuning that is too aggressive):

```
        4d
  Ku = ─────────────        Tu = mean period of the last N cycles
       π√(a² − h²)
```

### Deriving gains

Tyreus–Luyben for PI, chosen over Ziegler–Nichols because ZN targets quarter-
amplitude damping — roughly 50 % overshoot — which on a floor that cannot be
discharged is the wrong objective entirely:

```
  Kp = Ku / 3.2
  Ti = 2.2 · Tu
  Ki = Kp / Ti
  Kd = 0                     ← deliberate; see "the plant" above
```

For comparison, ZN PI would give `Kp = Ku/2.2`, `Ti = Tu/1.2` — roughly 1.5×
the proportional gain and an integral time an order of magnitude shorter. On
this plant that is a recipe for hunting.

### State machine

```
   Idle
    │  POST /api/autotune/start
    ▼
   Settling ──── wait for dPV/dt below threshold, bounded by a timeout
    │            (a run started on a moving temperature identifies nothing)
    ▼
   Oscillating ─ relay switching; record peak times and magnitudes
    │
    ├── converged: |Tu(n) − Tu(n−1)| < 15% and |a(n) − a(n−1)| < 20%
    │   over N consecutive cycles (N = 3)
    │        │
    │        ▼
    │      Done ──── gains computed, held for review or auto-applied
    │
    ├── safety envelope breached ──► Aborted (reason recorded)
    ├── max duration exceeded     ──► Aborted
    └── POST /api/autotune/abort  ──► Aborted
                                        │
                                        ▼
                            output off, previous gains untouched
```

### Safety envelope

The experiment deliberately makes the room oscillate, so it needs bounds that
are tighter than the normal controller's:

- absolute temperature ceiling and floor, defaulting to `SP ± 3 K`, and
  independent of the over-temperature shutoff from stage 1
- a maximum run duration (default 24 h) — an underfloor loop that has not
  produced three clean limit cycles in a day is not going to
- sensor loss aborts immediately; a relay experiment with no feedback is just a
  valve stuck open
- the stage-1 minimum dwell still applies, so the relay cannot chatter

### Duration, honestly

With `L` of 15–60 min, one limit cycle period `Tu` is typically 2–4 × the dead
time, so **1–4 hours per cycle**, and three cycles plus settling puts a
realistic run at **6–20 hours**. This is an overnight procedure, not a button
that returns an answer. That shapes the UI: it needs progress, an estimated
completion, a visible abort, and it must survive the browser being closed.

### Reboot policy: abort, do not resume

Recording enough state in NVS to resume a half-finished identification is
possible, and is the wrong thing to build. A reboot mid-run means an unknown
gap in the observation — the peaks that would have occurred during the outage
are lost, and the resulting `Tu` is a fiction. The run is marked aborted with
the reason, the previous gains are untouched, and the user starts again. The
only persisted state is "an autotune was running when we died", so the UI can
explain the abort rather than silently forgetting.

### Applying the result

Default is **propose, do not apply**: the run finishes, the computed `Kp`/`Ki`
are shown alongside the current values, and the user accepts. Auto-apply is
available as an explicit opt-in at start. The asymmetry is deliberate — an
autotune that silently rewrites the gains of a heating system while nobody is
watching is a poor default, and the computed values are worth a human glance
given the run takes all night.

## Staging and stopping points

```
  Stage 1 ──► device heats a room. Fixed conservative gains.
     │        Useful on its own; everything after is refinement.
     ▼
  Stage 2 ──► loop runs at a sane cadence
     │
     ▼
  Stage 3 ──► gains adjustable by hand; a competent user can tune it
     │        by trial and error from here
     ▼
  Stage 4 ──► the device tunes itself
```

Stages 1–3 are worth doing even if stage 4 is never built: they are three
unimplemented spec requirements plus the actuator the product is named for. If
scope has to be cut, cut from the end.