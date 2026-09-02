# Design: RelayAutotuner core

## Interface

The component is a state machine polled once per control tick. It never reads a
clock, never touches I/O, and never stores anything.

```cpp
namespace Control {
    enum class AutotuneState  { Idle, Settling, Oscillating, Done, Aborted };
    enum class AutotuneAbort  { None, UserRequested, CeilingBreached, FloorBreached,
                                SensorLost, RunTimeout, SettlingTimeout,
                                AmplitudeTooSmall, DerivedGainsInvalid };

    struct AutotuneLimits {
        float    hysteresis      = 0.15f;   // h, K — several x sensor resolution
        float    relayAmplitude  = 0.5f;    // d, half the 0..1 output swing
        float    ceilingOffset   = 3.0f;    // K above setpoint
        float    floorOffset     = 3.0f;    // K below setpoint
        uint32_t maxDurationMs   = 24u * 3600u * 1000u;
        uint32_t settlingTimeoutMs = 30u * 60u * 1000u;
        float    settlingRateKPerMin = 0.05f;
        uint8_t  requiredCycles  = 3;
    };

    struct AutotuneResult {
        float ku = 0, tu = 0;               // identified
        float kp = 0, ki = 0, kd = 0;       // derived (kd always 0)
    };

    class RelayAutotuner {
    public:
        explicit RelayAutotuner(AutotuneLimits limits);
        void  start(float setpoint, uint32_t nowMs);
        void  cancel();                      // -> Aborted/UserRequested
        float update(float temperature, bool dataValid, uint32_t nowMs);
        AutotuneState  state()  const;
        AutotuneAbort  abortReason() const;
        AutotuneResult result() const;       // valid only in Done
        uint8_t completedCycles() const;
        uint32_t elapsedMs(uint32_t nowMs) const;
    };
}
```

`update()` returns the commanded output level — `0.0` or `1.0` while
oscillating, `0.0` in every other state. Returning a level rather than a bool
keeps the eventual caller uniform with `PidController::update()`, so whatever
consumes the control output does not care which one produced it.

## State machine

```
   Idle ──start()──► Settling
                        │  |dT/dt| < settlingRateKPerMin
                        ▼
                   Oscillating ──── relay switches on hysteresis,
                        │            peaks recorded per half-cycle
                        │
      requiredCycles agree within tolerance
                        ▼
                      Done  (result populated; nothing applied)

   any state ──► Aborted, with a reason, output forced to 0
```

Settling exists because a run started while the temperature is still moving
identifies the disturbance rather than the plant. It has its own timeout so a
draughty room cannot leave a run parked forever.

## The measurement

```
  T ┤      ╭──╮        ╭──╮        ╭──╮      switch ON  below SP - h
    ┤     ╱    ╲      ╱    ╲      ╱    ╲     switch OFF above SP + h
  SP├────╱──────╲────╱──────╲────╱──────╲──
    ┤   ╱        ╲  ╱        ╲  ╱        ╲   a  = (max - min) / 2 per cycle
    ┤  ╯          ╲╯          ╲╯          ╲  Tu = peak-to-peak interval
    └──────────────────────────────────────
 out ▄▄▄▄____▄▄▄▄____▄▄▄▄____▄▄▄▄____
```

A cycle is bounded by two successive ON transitions; within it the running max
and min give `a`, and the interval between like transitions gives `Tu`.

Ultimate gain, hysteresis-corrected:

```
        4d
  Ku = ─────────────
       π√(a² − h²)
```

The uncorrected `4d/(πa)` over-estimates `Ku` and yields tuning that is too
aggressive; on a plant that cannot shed heat quickly that is the expensive
direction to err. If `a ≤ h` the expression is undefined — the oscillation never
cleared the hysteresis band, so there is nothing to identify and the run aborts
with `AmplitudeTooSmall` rather than producing a nonsense number.

Convergence requires `requiredCycles` consecutive cycles agreeing within 15 % on
period and 20 % on amplitude. Two cycles can agree by luck while the plant is
still settling; three is the usual recommendation and is what the earlier design
recorded.

## Derivation

Tyreus–Luyben, PI only:

```
  Kp = Ku / 3.2
  Ti = 2.2 · Tu
  Ki = Kp / Ti
  Kd = 0
```

Not Ziegler–Nichols: ZN targets quarter-amplitude damping, roughly 50 %
overshoot, which in a concrete floor takes hours to bleed off. `Kd = 0` is
deliberate — on a lag-dominant plant the derivative amplifies sensor noise and
contributes nothing.

Note `Ki` here is the same parameterisation `PidController` already uses
(`integral += Ki · error · dt`), so the derived value drops straight in.

## Testing against a simulated plant

The reason this change is worth doing before the actuator: every interesting
behaviour is reachable in milliseconds.

A first-order-plus-dead-time plant is enough to produce a realistic limit cycle:

```
  dT/dt = ( K·u(t − L) − (T − T_ambient) ) / τ

  K  process gain      L  dead time (15–60 min)     τ  time constant (2–8 h)
```

Implemented as a small test helper with a delay queue for `L` and an Euler step
for `τ`. Driving it at a simulated 60 s tick, a 20-hour run completes in a few
thousand iterations.

What that buys, none of which is practical on hardware:

| Test | Why it is hard otherwise |
|---|---|
| Full run to convergence | 6–20 hours per attempt |
| Derived gains match hand-computed Tyreus–Luyben | needs a known `Ku`/`Tu`, i.e. a known plant |
| Non-convergent plant times out | requires deliberately building a bad plant |
| `a ≤ h` aborts | requires a plant that barely responds |
| Ceiling / floor breach | requires overheating a real room |
| Sensor loss mid-run | requires unplugging a sensor at the right moment |
| `millis()` rollover mid-run | requires 49.7 days of uptime |

The plant helper is a test fixture, not production code, and lives in the test
directory.

## What deliberately is not here

`update()` returning a level that nothing consumes is the whole point of the
split. Wiring it up means deciding what happens when the PID and the autotuner
both want the output, which is a question about the actuator — and the actuator
is now an MQTT boolean whose failure modes (QoS 0, last-will, retained
messages) are unsettled. Building the identification logic against those
unknowns would be guessing; building it against a simulated plant is not.
