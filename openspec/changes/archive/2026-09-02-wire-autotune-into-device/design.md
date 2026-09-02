# Design: wiring the autotuner in

## Ownership of the control output

```
  SensorMonitor tick
        │
        ├─ consume start/cancel requests   (atomics set by the web task)
        │
        ├─ run active?
        │     yes ──► pid.suspend()
        │             lastControlOutput = autotuner.update(...)
        │
        │     no  ──► existing PID path, which restarts bumplessly because
        │             the suspend above already marked the gap
        ▼
```

The autotuner and the PID never both drive the output. Suspending the PID for
the duration of a run is not just tidiness: it is what makes the handover back
correct, because the bumpless-restart machinery already treats "did not run last
tick" as the signal to reseat its timestamp and accumulators. A run of any
length is exactly the gap that logic was built for.

## Start and cancel are requests, not calls

`start()` and `cancel()` mutate the autotuner's state machine. The web task must
not call them directly, for the same reason `setControlEnabled()` does not reset
the PID: the Sensor Monitor task is mid-`update()` on that state, and a
concurrent write can be straddled and lost.

So the web task sets an atomic flag and the control loop consumes it:

```
  web task                     SensorMonitor task
  --------                     ------------------
  startRequested = true   ──►  if (startRequested.exchange(false))
  (returns 202-ish)                autotuner.start(setpoint, now)
```

`exchange(false)` makes consumption atomic, so a request cannot be handled
twice. Cancel is consumed before start, so a cancel and a start arriving in the
same tick resolve to "cancel, then start" rather than being ordered by luck.

The cost is that a start is acknowledged before it takes effect — up to one
control tick later. The status endpoint is the source of truth for whether a run
is actually running, which the UI already has to poll anyway.

## Guards

A run is refused when control is disabled or one is already active, matching the
spec written in `add-heating-actuator-and-autotune`. Both are checked on the web
task against state it may read safely, and re-checked in the control loop: the
loop is authoritative, and the gap between request and consumption is real.

If control is switched off while a run is active, the loop cancels the run
rather than leaving it owning an output nobody enabled.

## Accepting a result

`PidController::setGains()` applies the derived gains and suspends, because an
integral accumulated under different gains means something different afterwards
— the same discontinuity argument used for the bumpless restart.

The gains are **not persisted**. `DeviceConfig` has no fields for them. That is
the separate "PID parameter configurability" requirement, and inventing half of
it here would mean writing NVS keys that the real change then has to migrate.
So acceptance is in-memory and the UI says so; a reboot returns the compiled-in
defaults.

## Why the timeout is the expected outcome

With no heating output, the relay command changes nothing, the temperature
follows the room rather than the experiment, and no limit cycle forms. Either
the temperature never settles (`SettlingTimeout`) or it settles and never
oscillates (`RunTimeout`).

That is not a defect and the UI must not let it read as one. The panel states
before the run that convergence is impossible until an actuator exists, and
reports the abort reason verbatim when it happens. What a run *does* prove today
is the ownership handoff, the guards, the abort paths, the endpoint contract and
the UI — everything except the identification itself, which is already covered
by the simulated-plant tests.
