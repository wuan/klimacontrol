> **STATUS: ON HOLD — output transport changed to MQTT (2026-09-02)**
>
> The decision is to drive a **boolean actuator over MQTT** rather than a local
> GPIO relay, and to pick this up later. Nothing here has been implemented
> (0/30 tasks). This document is kept for the parts that survive the change of
> transport, but **it must not be implemented as written** — re-propose when the
> work is picked up.
>
> **Void — all of it GPIO-specific:**
> - Stage 0 pin selection, strapping-pin avoidance, GPIO37/`SPI.begin()` caveat
> - The relay/SSR module choice and the active-high requirement
> - `ActuatorPins.h`, boot-safe pin-LOW initialisation, the wiring document
> - The `heating-actuator` spec's "Physical output pin" and "Boot-safe and
>   failsafe output state" requirements
> - The mains-wiring impact note
>
> **Still correct and still wanted:**
> - Time-proportional output — a boolean actuator over MQTT needs duty-to-on/off
>   conversion exactly as a relay does, including the cycle time and the
>   minimum-dwell rule tied to actuator travel time
> - Redefining `isControlActive()` as "actuator commanded on"
> - The over-temperature shutoff (already an unimplemented spec requirement)
> - Decimating the control loop to ~60 s
> - Tunable, persisted PID gains (also already an unimplemented requirement)
> - The whole autotune design: relay method, hysteresis-corrected `Ku`,
>   Tyreus-Luyben PI, `Kd = 0`, the state machine, the safety envelope, and
>   abort-rather-than-resume
>
> **New questions MQTT introduces, none of them answered here:**
> - **PubSubClient publishes at QoS 0 only.** There is no delivery guarantee, so
>   a dropped "off" leaves the actuator on. For a heating output that is the
>   central safety question of this design, and it has no equivalent in the
>   GPIO version.
> - **Failsafe moves from wiring to the broker.** An undriven pin closing a
>   normally-closed valve is replaced by an MQTT Last Will and Testament, plus a
>   decision about retained messages. A retained "on" command replayed after a
>   broker restart could open the actuator with nobody asking.
> - **Control now depends on the network and the broker.** The behaviour when
>   MQTT is unreachable has to be specified, where a GPIO could not fail that
>   way.
> - MQTT is disabled by default (`mqtt_enabled = false`), so enabling control
>   would gain a dependency on MQTT configuration being valid.
> - Topic design: command topic, state topic, and whether to emit Home Assistant
>   discovery.

# Close the control loop: actuator output, then relay autotune

## Why

The temperature controller computes an output and throws it away.
`SensorMonitor.cpp:68` calls `updateControl()` and discards the returned float.
Nothing in `src/` drives a relay, a GPIO or an MQTT command; the only consumer of
the control output is `isControlActive()`, which feeds the `●` symbol on the
dashboard and the e-paper footer.

```
  sensors ──► PID ──► output (0..1) ──► lastControlOutput ──► ● symbol
                                                              on screen
                            │
                            └──► nothing. No valve. No relay.
```

So the device is, today, a thermometer that displays an opinion. Everything
downstream of that opinion is missing.

This matters for the autotune request specifically: autotune is an
*identification experiment*. It perturbs the actuator and measures how the plant
answers. With no actuator there is nothing to perturb, so autotune is not a
feature that can be bolted on — it is downstream of building the output path.

### The shipped loop is also mistuned by orders of magnitude

For underfloor heating — the target application, with an on/off valve and a
response measured in tens of minutes — the current parameters are not merely
imperfect, they are the wrong scale:

| | Shipped | Underfloor reality |
|---|---|---|
| Control tick | 1 s (`SensorMonitor.h:23`) | plant time constant is hours; ~10⁴× oversampled |
| `Ki` | `0.1` s⁻¹ | drives the integral 0 → 100 % in 10 s at 1 K error |
| `Kd` | `0.5` | derivative is actively harmful on a lag-dominant plant |
| Output type | continuous `0.0…1.0` | valve accepts only open or closed |
| Safety limits | none in `updateControl()` | spec already requires over-temperature shutoff |
| Gains | `constexpr` in an anonymous namespace | spec already requires them to be tunable |

The last two rows are existing requirements that were written and never
implemented — `temperature-control` §"Safety limits" and §"PID parameter
configurability". Tunable gains are a hard prerequisite for autotune, which has
nowhere to write its result otherwise.

## What Changes

Four stages. Each is independently useful and independently shippable; the
tasks are ordered so the work can stop after any of them and leave the device in
a coherent state.

### Stage 1 — Actuator output and safety limits

A GPIO drives an external relay or SSR, which switches the zone valve. Because
the valve is on/off, the PID's `0.0…1.0` becomes a duty cycle via
**time-proportional output**: within a cycle of `T_cycle`, the valve is held open
for `duty × T_cycle` and closed for the rest.

```
  PID out = 0.30, cycle = 15 min
  ├──── OPEN 4.5 min ────┼─────────── CLOSED 10.5 min ───────────┤

  Constraint: thermal wax actuators take 3–5 min to travel. T_cycle must be
  several times that, or the valve never reaches either end stop and the duty
  cycle stops corresponding to heat delivered.
```

Alongside it, the over-temperature shutoff the spec already requires, and a
boot-safe output state (valve closed while the pin is undriven during reset).

`isControlActive()` is redefined from "output > 0" to "valve is currently open",
which is what the dashboard symbol and the panel footer should have been showing
all along.

### Stage 2 — Control cadence

Decimate the control tick to a configurable interval (default 60 s) while
sensors keep reading at 1 s. A PID whose derivative and integral are scaled by a
`dt` four orders of magnitude shorter than the plant's dynamics is integrating
sensor noise, not process error.

### Stage 3 — Tunable, persisted gains

`Kp`, `Ki`, `Kd`, the cycle time and the safety limits move from `constexpr`
into `DeviceConfig`, persist to NVS, and gain API and UI surfaces. This
satisfies the existing §"PID parameter configurability" requirement and gives
autotune somewhere to put its answer.

### Stage 4 — Relay autotune

An Åström–Hägglund relay experiment. The actuator is already a relay, so the
method needs no capability the device will not already have after stage 1: drive
the output bang-bang around the setpoint, measure the amplitude and period of
the induced limit cycle, and derive gains from them.

Derived with **Tyreus–Luyben**, not classic Ziegler–Nichols. ZN is far too
aggressive for a lag-dominant plant, and overshoot in a concrete floor takes
hours to bleed off. The result is a **PI** controller: `Kd` is set to zero
because on this plant the derivative term amplifies sensor noise and contributes
nothing useful.

## Non-goals

- **Multi-zone control.** A real manifold has several zone actuators. This
  change is one zone, one sensor, one output. Multi-zone is a larger data-model
  change and should not ride along.
- **Weather compensation / outdoor reset.** A well-understood improvement for
  underfloor, and orthogonal to closing the loop.
- **Cooling.** Output stays `[0, 1]`; negative demand remains "valve closed".
- **Publishing valve state over MQTT** and **showing autotune progress on the
  e-paper panel.** Both are natural follow-ups, both are additive, neither is
  needed to close the loop.
- **Resuming an interrupted autotune across a reboot.** See `design.md` — a
  half-finished identification is not worth trusting; the run aborts and says so.
- **Mains wiring guidance beyond a pointer to a suitable isolated relay
  module.** Zone actuators are commonly 230 V AC. The firmware drives a
  low-voltage control input; what sits on the other side is a hardware decision
  documented, not designed, here.

## Capabilities

### New Capabilities

- `heating-actuator`: the physical output path — pin assignment, boot-safe
  state, time-proportional output, minimum on/off dwell, and the relationship
  between duty cycle and valve travel time.
- `pid-autotune`: the relay experiment — its state machine, convergence
  criteria, safety envelope, abort paths, and how derived gains are applied.

### Modified Capabilities

- `temperature-control`: gains and limits become configuration rather than
  compile-time constants; the control loop runs on its own cadence; the
  over-temperature shutoff is implemented; `isControlActive()` is redefined in
  terms of valve state.
- `configuration`: `DeviceConfig` gains PID, actuator and autotune fields.
- `http-api`: endpoints for reading and writing gains, and for starting,
  aborting and polling an autotune run.
- `web-interface`: a tuning section on the settings page, and autotune progress
  with an abort control.

## Impact

- **Source**: `src/control/` (new `TimeProportionalOutput`, `RelayAutotuner`;
  `PidController` gains become runtime), `src/SensorController.cpp`,
  `src/task/SensorMonitor.cpp`, `src/Config.{h,cpp}`, `src/PrefsKeys.h`,
  new `src/routes/TuningRoutes.cpp`.
- **Web**: `data/settings.html`, `data/control.html`.
- **Docs**: a new wiring document for the actuator, in the shape of
  `docs/EINK_DISPLAY_WIRING.md`.
- **Hardware**: a relay or SSR module, and a free GPIO. The pin budget is tight
  with the e-paper panel fitted — the panel already claims GPIO 36, 35, 18, 9, 8
  and 17 (`DisplayPins.h`). Pin choice must be verified against the board
  variant and must avoid strapping pins, whose boot-time level is not under
  firmware control. See `design.md` for the open question.
- **Safety**: this is the first change that makes the device actuate anything.
  A firmware fault now has a physical consequence — a valve held open. The
  watchdog interaction and the failsafe-on-crash behaviour are design concerns,
  not afterthoughts.
- **Prerequisite**: `add-web-control-ui` should be verified and archived first.
  It touches `SensorController::updateControl()` and `PidController`, which
  stage 1 and stage 3 both build on.