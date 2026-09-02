# Relay autotuner core, ahead of the actuator

## Why

Autotune is wanted for underfloor heating, but the output path is not built and
the transport has just changed: a boolean actuator driven over MQTT, deferred to
later. `add-heating-actuator-and-autotune` is on hold for that reason.

The autotuner does not have to wait. Its entire decision-making half is pure
logic with no I/O: given a temperature and a timestamp it produces a boolean
command and a state. Nothing in that needs a GPIO, a broker, or a valve. A
simulated plant closes the loop in tests, so the component can be built and
proven now and wired to whatever output eventually exists.

This is the pattern the codebase already uses twice. `Display::RefreshPolicy`
and `Control::PidController` are both Arduino-free with a caller-supplied clock,
and both are fully exercised in the `native` environment. The autotuner is a
better fit for it than either: it is a state machine whose interesting
behaviours — convergence, non-convergence, five distinct abort paths — are
tedious to provoke on real hardware and trivial to provoke against a simulated
plant.

It is also the part with the real engineering content. A run on hardware takes
6–20 hours; discovering a convergence bug that way is not a plan.

## What Changes

- **`Control::RelayAutotuner`** — Arduino-free, injected clock, no I/O. Owns the
  `Idle → Settling → Oscillating → Done/Aborted` state machine, relay switching
  with hysteresis, peak detection, convergence testing, the hysteresis-corrected
  ultimate-gain calculation, Tyreus–Luyben PI derivation, and the safety
  envelope with its abort reasons.
- **A simulated first-order-plus-dead-time plant in the tests**, so a full run
  can be driven to convergence in milliseconds of wall-clock time across a range
  of plausible underfloor dynamics.
- **Native tests** covering convergence, each abort path, the rollover, and the
  derivation arithmetic against hand-computed values.

The design is unchanged from `add-heating-actuator-and-autotune`; this change
carves out the actuator-independent slice of it and implements that. Where the
two documents disagree, this one is current.

## Non-goals

Everything that needs an output or a user, all deferred until the MQTT actuator
exists:

- **Driving anything.** The autotuner returns a commanded level; nothing
  consumes it yet. There is no wiring into `SensorController` or
  `SensorMonitor`, so a run cannot be started on a device.
- **Persisting or applying derived gains.** Gains remain `constexpr`. The
  autotuner reports a result; storing it is the separate, still-unimplemented
  "PID parameter configurability" requirement.
- **API endpoints and UI.** No `/api/autotune/*`, no progress view, no abort
  button.
- **The reboot marker**, which only matters once a run can actually be started.
- **Time-proportional output.** It belongs with the actuator, not here — during
  a run the autotuner commands full on or full off directly.

The consequence worth stating plainly: at the end of this change the device
behaves exactly as it does today. Nothing is reachable by a user. What exists is
a proven component and the confidence that the hard part is right.

## Capabilities

### Modified Capabilities

- `pid-autotune`: the requirements from `add-heating-actuator-and-autotune`,
  narrowed to those the core component can satisfy on its own. The requirements
  about starting runs over HTTP, persisting a reboot marker, and applying gains
  are explicitly left for the later change.

## Impact

- **Source**: new `src/control/RelayAutotuner.{h,cpp}`. Nothing existing is
  modified — no call sites, no config, no routes.
- **Tests**: new `test/test_relay_autotuner/`, plus a `build_src_filter` entry
  in the `native` environment.
- **Risk**: low by construction. The component is not referenced by firmware
  code, so it cannot change device behaviour; the binary grows only if something
  links it, which nothing does yet.
