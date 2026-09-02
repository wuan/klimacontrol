# Design: Shelly actuator

## Where the work happens

The one hard constraint: an HTTP call must never sit on the control loop. The
Sensor Monitor task feeds a watchdog and reads sensors every second; a manifold
that has gone away and is taking 5 s to time out would stall it.

```
  SensorMonitor task            actuator task (or the Network task)
  ──────────────────            ───────────────────────────────────
  PID computes demand           reads demand
        │                             │
        └──► atomic<float> ───────────┘
             demand 0..1              TimeProportionalOutput
                                      decides open / closed
                                             │
                                             └──► HTTP Switch.Set
                                                  HTTP Switch.GetStatus
                                             ┌──── observed + apower
             atomic<Observed> ◄──────────────┘
```

One atomic float out, one small observed-state struct back. Each piece of state
has exactly one writer, which is the rule already established for the PID
accumulators and the autotuner, and for the same reason.

**Time-proportional output lives on the actuator side, not in the control loop.**
The loop's job is "how much heat", at 60 s. The actuator's job is "is the valve
open right now", at ~10 s. Putting TPO with the thing that owns the timing keeps
the cadences from having to agree, and mirrors how a physical TPO relay works —
it takes a demand and does the chopping itself.

## The dwell rule creates a dead zone, so demand is banked

The minimum-dwell rule is right — a valve that never reaches an end stop
delivers heat unrelated to the duty. Applied as plain snapping it is also
disastrous, because it makes whole ranges of demand unreachable:

```
  achievable duty  =  {0} u [T/C, 1-T/C] u {1}

  C=20 min, T=3 min   ->  {0} u [0.15 .. 0.85] u {1}
```

Underfloor heating in mild weather sits at 10-20 % duty, which is most of the
heating season. In that dead zone the loop cannot settle: it alternates between
nothing and the minimum, the integral winds and unwinds, and the quantisation
underneath defeats whatever tuning sits on top — including an autotune result.

So a shortfall is banked rather than discarded:

```
  each cycle   credit += demand x cycle
  spend        when credit >= one stroke, deliver a full-length pulse
  carry        otherwise nothing this cycle; credit persists

  5 % demand, 20 min cycle, 3 min stroke
    c0 credit 1m   -> closed      c1 credit 2m   -> closed
    c2 credit 3m   -> OPEN 3m     c3 credit 1m   -> closed   ...
    average 3 min per 60 min = 5 %
```

The rail at the top works the same way in reverse: 0.95 rounds up to a fully
open cycle, which overdelivers, pushes credit negative, and the debt is worked
off over following cycles. No individual cycle can represent the demand; the
average tracks it exactly.

Credit is discarded when demand reaches zero — heat asked for before the reason
for it disappeared should not arrive later — and on reset.

**Defaults: 20 minute cycle, 3 minute stroke.** Chosen together: 4 x 3 = 12 <= 20
satisfies the validity rule, and the confirmed actuator travel time is 3 minutes.
An earlier draft specified 15 minutes and 5 minutes, which violated its own
rule; the unit tests caught it on the first run.

## Cadences

```
  sensor read       1 s      unchanged
  actuator tick    10 s      evaluate TPO phase; renew if open
  PID compute      60 s      the decimation planned for a while
  TPO cycle        15 min    >= 4x actuator travel; caps cycling at 4/hour
  auto_off lease  180 s      6x the renewal interval
```

The lease being six renewals wide is deliberate: a single failed HTTP call must
not move a valve that takes three to five minutes per stroke. With
request/response we will *know* a call failed, which makes "log it and let the
next tick fix it" a defensible response rather than a hopeful one.

## Renewal, and what closes the valve

`auto_off` resets its countdown on every on-command, so renewal is simply
re-issuing `Switch.Set {on: true}`.

```
  ├──── OPEN ────┼─────────── CLOSED ───────────┤
  ▲    ▲    ▲    ▲
  on   renew     explicit off (immediate, precise)
       every 10s

  the lease never fires in normal operation.
  it is what runs when nothing else does.
```

Explicit off rather than letting the lease expire, because expiry would add up
to 180 s of unrequested heat to every cycle — on a 4.5-minute open phase that is
a 60 % error in delivered energy. Precision and safety are different jobs.

`toggle_after` on `Switch.Set` would fold them together elegantly — set the TTL
to the remaining open time and a lost off-command costs nothing. It is
unverified on firmware 2.0.0 and cannot be tested on a live appliance, so it is
an optimisation to confirm on a spare channel, not a dependency.

## Refusing to drive an unsafe actuator

The runbook's contract, enforced in firmware because every line of it reads back
from `Switch.GetConfig`:

```
  auto_off       true          the lease
  auto_off_delay >= 2x renewal  wide enough that one failure moves nothing
  initial_state  "off"          not restore_last — a relay reboot must not
                                re-open a valve with no controller alive
  in_mode        "detached"     a physical input must not fight the controller
```

Checked when control is enabled and periodically thereafter, because somebody
can change a Shelly setting at any time from its own web UI. A misconfigured
channel is a startup error, not a latent hazard. This is the thing a soldered
wire could never offer.

## Commanded, observed, confirmed

With a remote relay, `isControlActive()` stops being a fact and becomes a
belief. Two independent observations make it a fact again:

```
  commanded   what we last successfully sent
  output      what the relay says its contact is doing
  apower      whether a wax head actually drew current
```

| commanded | output | apower | meaning |
|---|---|---|---|
| off | false | ~0 | agreed, closed |
| on | true | ~2-3 W | agreed, heating |
| on | true | **~0** | **relay closed, no actuator** — dead or disconnected head |
| on | **false** | ~0 | relay refused or was overridden |
| — | *stale* | — | manifold unreachable |

Row three is the one worth the effort. A failed wax actuator is otherwise
completely invisible: the valve reports open, the controller believes it is
heating, and the room simply never warms. Only the current draw tells you.

The panel's `●` and the demand bar must follow *confirmed* state, not intent.
A green dot during a manifold outage is the display lying about the one thing
someone looks at it for.

## What still cannot be guaranteed here

The lease lives in the relay and is the only mechanism that survives this
firmware hanging. Everything in this change is advisory by comparison. That is
why configuration verification is a hard gate rather than a warning: if the
lease is not enabled, none of the safety reasoning in this document holds, and
the honest response is to refuse to drive the channel at all.
