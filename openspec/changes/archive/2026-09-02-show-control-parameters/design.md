# Design: control parameters panel

## Why a separate endpoint

`/api/status` is polled every 10 s by every open client, and it already does
more work than it should — `StatusRoutes.cpp:23` calls `loadDeviceConfig()`,
reopening NVS on each request. Control diagnostics are looked at deliberately,
by one person, for a few minutes. They belong behind a request that only happens
when someone is looking.

The dashboard already has this idiom: "Show Measurements" fetches
`/api/measurements` on demand and renders a table. The control panel follows it
exactly, so the page gains no new patterns.

```
  /api/status      every 10 s, every client   → the three summary fields
  /api/control     on demand, one client      → gains + live loop state
```

## Response shape

```json
{
  "enabled": true,
  "running": true,
  "setpoint": 22.0,
  "temperature": 21.4,
  "error": 0.6,
  "output": 0.42,
  "integral": 0.18,
  "kp": 2.0, "ki": 0.1, "kd": 0.5,
  "output_min": 0.0, "output_max": 1.0
}
```

`error` is derived server-side rather than left to the client, so the UI cannot
disagree with the controller about the sign convention (`setpoint − temperature`).

`temperature` and `error` are omitted when no valid reading exists, matching how
`/api/status` already omits `temperature` on a NaN, rather than inventing a
value the client would have to special-case.

## Demand, not a dot

The dashboard keeps its `−` / `○` / `●` symbol — that is a glanceable summary
and the e-paper footer mirrors it. The panel adds the number behind it:

```
┌─ Control Parameters ───────────────────┐
│ State           Enabled, running       │
│ Demand          42 %      ▓▓▓▓▓░░░░░░  │
│ Setpoint        22.0 °C                │
│ Temperature     21.4 °C                │
│ Error           +0.6 K                 │
│ Integral        0.18                   │
│ Kp / Ki / Kd    2.0 / 0.1 / 0.5        │
└────────────────────────────────────────┘
```

Demand is `output` scaled across `[output_min, output_max]`. Until an actuator
exists this is the only observable result of the loop, and once one exists it is
exactly the value time-proportional output will convert into on/off time — so
the number does not change meaning when the actuator lands.

The bar is worth the few lines it costs: a saturated controller sitting at 100 %
looks obviously different from one modulating at 40 %, which is the single most
useful thing to see while judging gains.

## Exposing the gains

They currently sit in an anonymous namespace in `SensorController.cpp`, so they
are not merely private, they are invisible outside the translation unit. Moving
them to `Control` namespace scope in `PidController.h` as documented defaults is
the smallest change that makes them readable, and it puts them next to the type
that consumes them.

They stay `constexpr`. Making them editable is a larger, separate piece of work
— `DeviceConfig`, NVS, validation, a write endpoint — and it is already written
down as an unimplemented requirement. Reading them is useful on its own and does
not prejudge how writing them should work.

## Accessors

`SensorController` gains four const accessors: `getControlOutput()`,
`getControlGains()`, `getControlIntegral()`, `isControlRunning()`. The last two
forward to `PidController`, which already exposes them as test seams.

No locking is added. These are single 32-bit reads of members written only by
the Sensor Monitor task, on a single-core part, and the existing
`isControlActive()` already reads `lastControlOutput` from the web task the same
way. A torn read is not possible and a stale-by-one-tick value is exactly what a
diagnostic panel should show.
