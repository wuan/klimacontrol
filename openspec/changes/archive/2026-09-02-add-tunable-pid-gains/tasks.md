# Tasks: tunable, persisted PID gains and a sane control cadence

Ordered so the work is testable in `native` before it needs a device, and so it
is useful after section 4. If scope has to be cut, cut from the end — but not
section 3, which fixes a shipped race.

## 1. Config fields, defaults and validation

- [x] 1.1 Add `kp`, `ki`, `kd` and `control_interval_s` to
      `Config::DeviceConfig` (`src/Config.h`), with the bound constants
      (`MIN_/MAX_/DEFAULT_`) alongside the existing TPO and safety constants
- [x] 1.2 Set the new defaults per design D5: `kp = 0.5`, `ki = 0.0001`,
      `kd = 0.0`, `control_interval_s = 60`. Leave a comment recording that
      these are derived arithmetically and are expected to be revised after the
      first converged run on a real plant
- [x] 1.3 Add NVS keys `"pid_kp"`, `"pid_ki"`, `"pid_kd"`, `"ctrl_intv"` as
      private `ConfigManager` constants (the `Config.h:258+` convention, not
      `PrefsKeys.h`), each with a `static_assert(nvsKeyFits(...))`
- [x] 1.4 Extend `validateDeviceConfig()`: per-field range check with fallback
      to the default on non-finite or out-of-range, and reject `kp == 0`
- [x] 1.5 Load and save the four fields in `loadDeviceConfig()` /
      `saveDeviceConfig()`
- [x] 1.6 Add `ConfigManager::updateTuning(kp, ki, kd, intervalS)` as a partial
      update in the shape of `updateActuatorTiming()`
- [x] 1.7 Tests (`pio test -e native`): per-field range validation; NaN and
      infinity fall back; `kp == 0` falls back; `ki == 0` and `kd == 0` are
      preserved; round-trip through `updateTuning()`

## 2. Control loop decimation

- [x] 2.1 Add a `lastPidComputeMs` member to `SensorController` and guard the
      `pid.update()` call in `updateControl()` on
      `now - lastPidComputeMs >= control_interval_s * 1000`, unsigned so it is
      rollover-correct (design D3)
- [x] 2.2 **Verify by reading the code** that the guard encloses only
      `pid.update()` — not the over-temperature shutoff at the top of
      `updateControl()`, and not the `autotuner.update()` branch (design D1, D2)
- [x] 2.3 On a non-computing tick, leave `lastControlOutput` at the last
      computed value rather than zeroing it, so demand is held across the
      interval rather than pulsing once per interval. Confirm this against what
      `HeatingActuator` reads and what `isControlActive()` reports
- [x] 2.4 Reseat `lastPidComputeMs` wherever `pid.suspend()` is called, so a
      resumed controller does not immediately compute against a stale interval
      baseline
- [x] 2.5 Tests: PID computes once per N sensor ticks; the safety shutoff
      engages within one sensor tick of the limit being crossed with a 60 s
      interval configured; `autotuner.update()` is called on every tick during a
      run; decimation is correct across the `millis()` rollover; a suspended and
      resumed controller computes on its first eligible tick

## 3. Make gain changes single-writer (fixes a shipped race)

- [x] 3.1 Add an atomic pending-gains request to `SensorController`, following
      the `autotuneStartRequested` / `autotuneCancelRequested` pattern
- [x] 3.2 Consume the request at the top of `updateControl()` and call
      `pid.setGains()` from there — the control task — so the web task never
      touches PID state (design D4)
- [x] 3.3 Change `acceptAutotuneResult()` to persist via `updateTuning()` and
      raise the pending-gains request instead of calling `pid.setGains()`
      directly. Persist before requesting (design D7)
- [x] 3.4 Add a `requestGains()` entry point for the tuning endpoint that raises
      the same request
- [x] 3.5 Delete the now-false "In memory only: DeviceConfig has no gain fields"
      comment in `acceptAutotuneResult()`
- [x] 3.6 Re-read `PidController.h:58-62` — the comment asserting the web task
      never touches PID state — and confirm it is true again. Update it if the
      wording needs to name the request path
- [x] 3.7 Push config gains into `PidController` at construction or first tick,
      so a stored tuning is in force after a restart rather than the defaults
- [x] 3.8 Tests: a requested gain change is not visible until a tick has run;
      applying gains suspends the controller and the next tick restarts with a
      zero integral; stored gains are in force after construction

## 4. Tuning endpoint

- [x] 4.1 Add `POST /api/control/tuning` in `src/routes/ControlRoutes.cpp` with
      `verifyCsrfHeader()`, all-or-nothing validation, and an error naming the
      offending field
- [x] 4.2 Register it with `AsyncURIMatcher::exact` — the existing
      `server.on("/api/control", HTTP_GET, ...)` at line 55 uses the default
      matcher, which also matches deeper paths. Same trap as `/api/actuator`,
      already documented at `ControlRoutes.cpp:140-153`
- [x] 4.3 Extend `GET /api/control` with `control_interval_s`, and confirm the
      reported gains come from `pid.getGains()` (in force) rather than from
      `DeviceConfig` (stored), so a pending change is not reported as applied
- [x] 4.4 Verify the endpoint rejects rather than clamps, and returns the
      correct status code for a validation failure per the project's convention
- [x] 4.5 Confirm `POST /api/autotune/accept` still answers immediately and that
      its success now means "accepted", updating the response or the docs if the
      current wording implies the gains are already applied

## 5. Web UI

- [x] 5.1 Add a Tuning section to `data/settings.html`: four fields with units
      and ranges, its own save action, the derived `Ti = Kp / Ki` in seconds,
      and the note that these values drive a physical valve
- [x] 5.2 Handle `Ki = 0` in the `Ti` display without dividing by zero
- [x] 5.3 Surface a rejected save with the offending field named, leaving the
      entered values in the form
- [x] 5.4 Remove the "applied in memory only / lost on restart" wording from the
      autotune acceptance UI
- [x] 5.5 Make the "runs cannot converge" warning conditional on the actuator
      assignment. `GET /api/autotune/status` already carries the two messages
      (`ControlRoutes.cpp:367-368`); show neither when a conforming actuator is
      assigned
- [x] 5.6 Regenerate `src/generated/settings_gz.h` (and `control_gz.h` if
      `data/control.html` changed) via `scripts/compress_web.py` or a
      `pio run` pre-build

## 6. Verify and close out

- [x] 6.1 `pio test -e native` — all green
- [x] 6.2 `pio run -e adafruit_qtpy_esp32s2` — builds
- [ ] 6.3 On the bench unit: store a tuning, restart, confirm `GET /api/control`
      reports the stored gains and not the defaults
- [ ] 6.4 On the bench unit: confirm the PID output changes about once a minute
      with the default interval, while `/api/status` keeps updating every second
- [x] 6.5 Update the README's temperature-control section with the new defaults
      and the control interval
- [ ] 6.6 Reconsider the design's open questions in light of what the bench unit
      showed — in particular whether `control_interval_s` should be exposed in
      the UI at all
      - **Partly settled without bench data:** `control_interval_s` is exposed
        in the UI, per the design's stated leaning — hiding a persisted field
        makes it harder to diagnose. Still open, and genuinely dependent on a
        converged run on a real plant: whether the defaults are right, and
        whether the derivative term earns its keep.
- [x] 6.7 `/opsx:verify`, then `/opsx:archive`
