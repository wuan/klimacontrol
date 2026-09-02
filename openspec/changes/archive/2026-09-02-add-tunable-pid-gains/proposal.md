# Tunable, persisted PID gains and a sane control cadence

## Why

Autotune runs end to end and computes gains that cannot be kept.
`SensorController::acceptAutotuneResult()` says so in as many words:

```cpp
// In memory only: DeviceConfig has no gain fields, so a restart returns the
// compiled-in defaults.
```

A run takes 6–20 hours on an underfloor plant. Accepting the result applies it
until the next reboot — an OTA update, a brownout, a power cut — and then the
device silently reverts to `Kp=2.0, Ki=0.1, Kd=0.5`. The whole identification
experiment is spent on a value with a lifetime shorter than the experiment.

This is not an oversight in the autotune work; it was scoped out deliberately
and recorded as a spec requirement (`pid-autotune` §"Derived gains are applied
in memory only"), pointing at the older, still-unimplemented
`temperature-control` §"PID parameter configurability". This change implements
that requirement and lets the autotuner's answer survive.

The shipped defaults are also the wrong scale for the target plant, and now that
the device actually drives a valve that matters:

| | Shipped | Underfloor reality |
|---|---|---|
| `Ki` | `0.1` s⁻¹ | drives the integral 0 → 100 % in 10 s at 1 K error |
| `Kd` | `0.5` | derivative on a lag-dominant plant amplifies sensor noise |
| Control tick | 1 s | plant time constant is hours |

The autotuner's own derivation says the same thing from the other direction: it
produces a **PI** controller with `Kd = 0` by Tyreus–Luyben, because derivative
action is actively harmful here. The compiled-in defaults contradict the tuning
method the firmware ships.

## What Changes

- `Kp`, `Ki`, `Kd` and a control interval move into `DeviceConfig`, persist to
  NVS, and are validated on load like every other field.
- The PID computation is decimated to a configurable interval (default 60 s)
  while sensors keep reading at 1 s. **The over-temperature shutoff and the
  autotuner tick are explicitly excluded** — see below; getting this wrong is
  the main risk in the change.
- Accepting an autotune result persists it, so the run's cost buys something
  durable. Acceptance stays an explicit user action, not automatic.
- The shipped defaults change to conservative PI values consistent with the
  autotuner's own derivation. **BREAKING** for behaviour, not for API: an
  existing device with no stored gains adopts the new defaults on upgrade. This
  is intended — the old defaults are not a tuning anyone chose, and no device is
  yet running a heating season on them.
- A tuning section on the settings page, and `POST /api/control/tuning`. No new
  read endpoint: `GET /api/control` already reports the gains and gains the
  control interval.

### What is deliberately not decimated

`updateControl()` has grown two other jobs since the cadence change was first
sketched, and only the PID computation may be slowed:

```
  updateControl()  ──► over-temperature shutoff   1 s   (safety; delaying it
                   │                                     by 60 s is a
                   │                                     regression)
                   ├─► autotuner.update()         1 s   (samples the limit
                   │                                     cycle; coarser
                   │                                     sampling biases Ku)
                   └─► pid.update()              60 s   ← the only decimated
                                                          part
```

The autotuner exclusion is the non-obvious one. It measures the half
peak-to-peak amplitude `a` of an induced oscillation and inverts it:
`Ku = 4d / (π · √(a² − h²))`. Sampling at 60 s can miss a peak, which
*under*-estimates `a` and therefore *over*-estimates `Ku` — yielding gains more
aggressive than the plant can take, from a run that reports success. A silent
accuracy loss in the one component whose entire purpose is measurement.

`updateControl()` must also still be *called* every tick even when it does not
compute, so `pid.suspend()` keeps marking skipped ticks. Decimating the call
rather than the computation reintroduces the stale-timestamp bug
`add-web-control-ui` fixed: the next computing tick would see a `dt` spanning
the whole gap and saturate the integral.

Note that decimation does not change the *meaning* of `Ki`. `PidController`
integrates `ki · error · dt` with `dt` in real seconds, so the accumulated term
per unit wall-clock time is unchanged. The gain is a quieter derivative and
3600 fewer float operations an hour, not a re-scaling.

## Capabilities

### New Capabilities

None. This implements requirements that already exist and were written as
unimplemented.

### Modified Capabilities

- `temperature-control`: §"PID parameter configurability" becomes concrete —
  gains are stored, persisted and validated, with a stated range per field and a
  gain change treated as a controller discontinuity. §"Control loop scheduling"
  gains the decimated cadence and states which of the loop's jobs run at the
  sensor tick regardless.
- `configuration`: `DeviceConfig` gains `kp`, `ki`, `kd` and `control_interval_s`
  with validation and defaults.
- `pid-autotune`: §"Derived gains are applied in memory only" is replaced —
  acceptance now persists, and the requirement that any interface disclaim
  durability is inverted into a requirement that it survive a restart.
- `http-api`: a tuning write endpoint; `GET /api/control` reports the control
  interval and reports the gains *in force* rather than those stored; the
  autotune accept endpoint's semantics become "request accepted" rather than
  "gains applied".
- `web-interface`: a tuning section on the settings page, and the autotune
  view's two stale limitation notices are corrected — acceptance is no longer
  temporary, and the "runs cannot converge" warning becomes conditional on the
  actuator assignment rather than unconditional.

## Impact

- **Source**: `src/Config.{h,cpp}` (four fields, NVS keys with the
  `nvsKeyFits()` `static_assert`, validation in `validateDeviceConfig()`, an
  `updateTuning()` partial-update method following `updateActuatorTiming()`),
  `src/SensorController.cpp` (`updateControl()` decimation;
  `acceptAutotuneResult()` persists), `src/routes/ControlRoutes.cpp` or a new
  `TuningRoutes.cpp`.
- **Web**: `data/settings.html`, `data/control.html`, and the regenerated
  `src/generated/*_gz.h`.
- **Tests** (`pio test -e native`): validation ranges per field, cross-field
  rejection, decimation arithmetic including the `millis()` rollover, that the
  safety shutoff and autotuner still see every tick, and that a gain change
  suspends the controller.
- **No hardware impact.** No new pins, no new dependencies, no change to the
  actuator path.
- **Not blocked.** Everything it builds on is shipped: `PidController::setGains()`
  already exists and already suspends, and `POST /api/autotune/accept` already
  applies a result in memory.
