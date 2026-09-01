# Tasks: Actuator output and relay autotune

Staged. Each stage leaves the device coherent, so work can stop after any of
them. If scope has to be cut, cut from the end.

## Stage 0 — Prerequisites and decisions

- [ ] **Verify and archive `add-web-control-ui`** — it modifies
      `updateControl()` and `PidController`, which stages 1 and 3 both build on
- [ ] **Settle the actuator GPIO** (blocking; hardware decision, see
      `design.md` §"Open question")
  - [ ] Read the QT Py ESP32-S2 variant file for the free broken-out pads;
        the panel already claims 36, 35, 18, 9, 8, 17 (`DisplayPins.h`)
  - [ ] Exclude strapping pins — their reset-time level is not under firmware
        control, and here that decides valve state during boot
  - [ ] Exclude GPIO37/MI, which `SPI.begin()` reclaims as an input
  - [ ] Confirm the chosen pad is not needed by the STEMMA QT bus or the
        NeoPixel
- [ ] **Choose the relay/SSR module** and confirm it is active-high, so an
      undriven pin means a de-energised relay and a closed NC valve. An
      active-low module inverts the entire failsafe story
- [ ] **Confirm the actuator travel time** from the datasheet of the fitted
      thermal actuator; the default cycle period depends on it

## Stage 1 — Actuator output and safety limits

- [ ] **`ActuatorPins.h`**, modelled on `DisplayPins.h`
  - [ ] Compile-time pin constant with `static_assert` collision guards against
        the panel pins, STEMMA QT, NeoPixel and USB pins
  - [ ] Comment recording why the pin is not runtime-configurable

- [ ] **Boot-safe initialisation**
  - [ ] Drive the pin LOW in `setup()` before the Sensor Monitor task starts
  - [ ] Verify with a meter that the pin is low from power-on through to the
        first control tick

- [ ] **`Control::TimeProportionalOutput`** (`src/control/`, Arduino-free like
      `PidController`, clock supplied by the caller)
  - [ ] Latch the duty at cycle start; do not re-evaluate mid-cycle
  - [ ] Snap duties below/above the minimum-dwell threshold to fully closed/open
  - [ ] Expose `isOpen()` for the status symbol
  - [ ] Handle the `millis()` rollover in the cycle arithmetic

- [ ] **Over-temperature shutoff** in `updateControl()`
  - [ ] Evaluate before the PID computation so a saturated integral cannot
        override it
  - [ ] Release with hysteresis so the output does not chatter at the threshold
  - [ ] Engage when no valid temperature is available

- [ ] **Wire the output**
  - [ ] `SensorMonitor` drives `TimeProportionalOutput` from the PID output
  - [ ] Redefine `isControlActive()` as "valve currently open"
  - [ ] Update `/api/status` and the e-paper footer to follow valve state

- [ ] **Tests** (`pio test -e native`)
  - [ ] Duty → open time for 0.0, 0.3, 1.0
  - [ ] Duty latched: a mid-cycle change takes effect next cycle
  - [ ] Minimum dwell snaps both rails
  - [ ] Cycle arithmetic correct across the `millis()` rollover
  - [ ] Over-temperature shutoff overrides a saturated integral
  - [ ] Shutoff hysteresis prevents chatter

- [ ] **Docs**: `docs/ACTUATOR_WIRING.md` in the shape of
      `docs/EINK_DISPLAY_WIRING.md`, stating the active-high/NC contract
      explicitly and warning that zone actuators are commonly mains voltage

- [ ] **Hardware verification**
  - [ ] Valve closed at boot, on crash (force a panic), on sensor unplug, and
        when control is disabled
  - [ ] A commanded 0.3 duty produces a 4.5 min open interval in a 15 min cycle
  - [ ] Room temperature actually converges on the setpoint over several hours

## Stage 2 — Control cadence

- [ ] Add `control_interval_s` to `DeviceConfig` (default 60)
- [ ] Decimate the PID computation in `SensorMonitor`; keep sensors at 1 s
- [ ] **Keep calling `updateControl()` on every sensor tick** so skipped ticks
      are still marked — decimate the computation, not the gating. An outer
      guard here reintroduces the stale-timestamp bug `add-web-control-ui` fixed
- [ ] Test: PID computes once per N ticks; suspension still marked on the
      intervening ticks

## Stage 3 — Tunable, persisted gains

- [ ] **Config**: `kp`, `ki`, `kd`, cycle period, travel time, control interval,
      safety limit and hysteresis in `DeviceConfig` + `PrefsKeys` + NVS
  - [ ] Documented range per field; load-time validation falls back to the
        default rather than clamping
  - [ ] Cross-field check: cycle period ≥ 4 × travel time, else both to defaults
  - [ ] Replace the underfloor-inappropriate defaults (`Ki = 0.1`, `Kd = 0.5`)
        with conservative values
- [ ] **`PidController`**: setter for gains; `SensorController` pushes config
      changes through and `suspend()`s on any gain change
- [ ] **`src/routes/TuningRoutes.cpp`**: `GET`/`POST /api/tuning`, all-or-
      nothing validation, CSRF header, reject rather than clamp
- [ ] **`data/settings.html`**: tuning section with units, ranges, and a warning
      that these drive a physical valve
- [ ] Regenerate `src/generated/settings_gz.h`
- [ ] Tests: per-field range validation; partial update applies nothing when any
      field is invalid; gain change suspends the controller; round-trip via NVS

## Stage 4 — Relay autotune

- [ ] **`Control::RelayAutotuner`** (Arduino-free, injected clock)
  - [ ] State machine: `Idle` → `Settling` → `Oscillating` → `Done`/`Aborted`
  - [ ] Settling gate on `dPV/dt`, with its own timeout
  - [ ] Peak detection; record amplitude and period per cycle
  - [ ] Convergence: 3 consecutive cycles within 15 % period, 20 % amplitude
  - [ ] `Ku = 4d / (π√(a² − h²))` — the hysteresis-corrected form
  - [ ] Abort when `a <= h` rather than producing an imaginary `Ku`
  - [ ] Tyreus–Luyben PI: `Kp = Ku/3.2`, `Ki = Kp/(2.2·Tu)`, `Kd = 0`
  - [ ] Range-validate derived gains; fail the run rather than store nonsense

- [ ] **Safety envelope**
  - [ ] Ceiling/floor at `setpoint ± 3 K`, independent of the stage-1 shutoff
  - [ ] Max duration (default 24 h) → abort with a non-convergence reason
  - [ ] Sensor loss → immediate abort
  - [ ] Stage-1 minimum dwell still applies, so the valve cannot chatter
  - [ ] Machine-readable abort reasons throughout

- [ ] **Integration**
  - [ ] Autotuner owns the output while running; PID resumes via `suspend()` on
        completion or abort
  - [ ] Refuse to start when control is disabled or a run is already active
  - [ ] Persist the run marker; on boot, report an interrupted run as aborted
        and clear it — do not resume

- [ ] **API**: `POST /api/autotune/start|abort|accept`,
      `GET /api/autotune/status`, all POSTs CSRF-guarded, 409 on conflicting
      state. Status must be sufficient to rebuild the whole view after a reload

- [ ] **UI**: progress, elapsed time, cycles completed, always-reachable abort,
      derived-vs-current gains with an explicit accept, and a pre-start note
      that this takes hours and will make the room oscillate

- [ ] Regenerate web assets

- [ ] **Tests** — the state machine is fully testable with an injected clock and
      a synthetic plant; this is the payoff for extracting `PidController`
  - [ ] Simulated FOPDT plant drives a full run to convergence
  - [ ] Derived gains match Tyreus–Luyben for a known `Ku`/`Tu`
  - [ ] Hysteresis correction applied; `a <= h` aborts
  - [ ] Each abort path: ceiling, floor, duration, sensor loss, user abort
  - [ ] Non-convergent plant times out rather than running forever
  - [ ] Gains unchanged after every abort path

- [ ] **Hardware verification**
  - [ ] A real overnight run on the actual floor
  - [ ] Abort mid-run leaves the valve closed and gains untouched
  - [ ] Power-cycle mid-run reports an interrupted abort at next boot
  - [ ] Resulting gains hold the room without hunting over 48 h