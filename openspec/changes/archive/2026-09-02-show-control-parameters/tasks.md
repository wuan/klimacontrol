# Tasks: Show control parameters in the web UI

## Backend

- [x] Move `PID_GAINS`, `MinOutput`, `MaxOutput` out of the anonymous namespace
      in `SensorController.cpp` to `Control` namespace scope in
      `PidController.h`, as documented defaults. They stay `constexpr`
- [x] Add const accessors to `SensorController`: `getControlOutput()`,
      `getControlGains()`, `getControlIntegral()`, `isControlRunning()`
- [x] `GET /api/control` in `ControlRoutes.cpp` returning the documented shape
  - [x] Compute `error` server-side as `setpoint − temperature`
  - [x] Omit `temperature` and `error` when no valid reading exists
  - [x] No CSRF requirement — it changes no state

## Web interface

- [x] Collapsible "Control Parameters" panel in `data/control.html`, following
      the existing Show/Clear Measurements idiom
- [x] Render state, demand percentage with a magnitude bar, setpoint,
      temperature, error, integral and gains
- [x] Refresh on the existing 10 s poll only while visible
- [x] Placeholders when temperature/error are absent
- [x] Keep the three-state symbol in the control bar
- [x] Regenerate `src/generated/control_gz.h`

## Verification

- [x] `pio test -e native` green, firmware builds
- [x] `GET /api/control` returns the documented fields on hardware
- [x] Values track a real control run: demand rises with error, integral
      accumulates, both reset on a bumpless restart
- [x] Panel renders and refreshes only while open

## What the panel immediately revealed

Verified against a live run with the setpoint 0.75 K above room temperature:

```
error 0.75 K -> output 1.00 (saturated)   integral 0.29 -> 1.00 in 20 s
```

`Kp = 2.0` means any error above 0.5 K saturates the output on its own, and
`Ki = 0.1 s⁻¹` drives the integral into its anti-windup clamp within twenty
seconds. The claim that the shipped gains are wrong for underfloor heating by
orders of magnitude was previously an argument on paper; it is now a thing you
can watch happen in the UI, which is most of the point of this change.
