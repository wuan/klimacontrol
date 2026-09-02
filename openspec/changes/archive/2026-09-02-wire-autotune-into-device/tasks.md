# Tasks: wire the autotuner into the device

## Control loop

- [x] `SensorController` owns a `Control::RelayAutotuner`
- [x] Atomic request flags for start and cancel, set by the web task
- [x] `updateControl()` consumes cancel before start, each exactly once
      (`exchange`), so same-tick arrivals resolve deterministically
- [x] While a run is active: `pid.suspend()`, and the autotuner drives
      `lastControlOutput`
- [x] Cancel the run if control is disabled mid-run
- [x] Re-check the guards in the loop, which is authoritative
- [x] `PidController::setGains()` applies and suspends

## API

- [x] `POST /api/autotune/start` — 409 when control is disabled or a run is
      active; CSRF required
- [x] `POST /api/autotune/abort` — CSRF required
- [x] `POST /api/autotune/accept` — 409 unless converged; CSRF required
- [x] `GET /api/autotune/status` — state, abort reason, elapsed, cycles,
      ku/tu, derived gains, gains in force; no CSRF

## Web interface

- [x] Autotune section in the control-parameters panel
- [x] Start / abort / accept controls, abort reachable whenever running
- [x] Progress rebuilt from the status endpoint, surviving a reload
- [x] State the two limitations plainly: runs cannot converge without a heating
      output, and accepted gains are not persisted
- [x] Regenerate `src/generated/control_gz.h`

## Verification

- [x] `pio test -e native` green; firmware builds
- [x] Start is refused with 409 while control is disabled
- [x] Start is refused with 409 while a run is active
- [x] A started run reaches `Settling` and reports through the status endpoint
- [x] Abort returns the output to zero and the PID resumes bumplessly
      (`PID restart:` in a `-DCORE_DEBUG_LEVEL=4` build)
- [x] Disabling control mid-run cancels it
- [ ] A run left alone ends in a timeout with the reason reported — not yet
      observed, since the settling timeout is 30 min and the run timeout 24 h.
      The abort *path* is verified (user abort, control-disabled cancel), and
      the timeout paths are covered against the simulated plant in
      `test_relay_autotuner`

## Verified on hardware

```
guards
  start, control disabled          -> 409
  start, run already active        -> 409
  accept, no converged result      -> 409
  start, no CSRF header            -> 403

ownership handoff (debug build)
  101030  Autotune starting around 25.1 C
          ... no PID ticks at all for the duration of the run
  111494  abort
  112029  PID restart: output=0.32 (proportional only)   integral 0.15 -> 0.02

during a run   output=1, pid running=false   autotuner owns the output
after abort    output=0, state=aborted, reason=user_requested
control off
  mid-run      run cancelled, state=aborted
```

The PID going completely silent for the run, then coming back through the
bumpless-restart path with a zeroed integral, is the interaction most likely to
have been wrong. It is the reason the autotuner suspends the PID rather than
merely out-voting it.
