# Tasks: Web control UI and bumpless PID restart

## Decision to settle before starting

- [x] **Choose the PID test strategy** (see `design.md` §"Open question")
  - [x] Option A — extract a `PidController` type with an injected clock, put
        the real logic under test, delete the mirror in
        `test_temperature_control.cpp:204` *(recommended)*
  - [ ] Option B — keep the mirror and update it in lockstep with every change
        to `updateControl()`
  - The task list below is written for Option A; under Option B, substitute
    "update the mirror" wherever a `PidController` test is named.

## Backend — bumpless restart

- [x] **Promote PID state out of function-local `static`s**
  - [x] Remove `static float integral`, `static float previousError`,
        `static uint32_t lastControlTime` from `SensorController.cpp:453-455`
  - [x] Under Option A they became private members of `Control::PidController`
        (`src/control/PidController.h`), which `SensorController` now owns as
        the `pid` member
  - [x] The resumption flag is `PidController::running`, set via `suspend()`
        and read via `isRunning()` — Option A's equivalent of the
        `controlWasRunning` member the Option B wording named

- [x] **Detect resumption inside the control loop**
  - [x] Every early-return path in `updateControl()` calls `pid.suspend()`
  - [x] `PidController::update()` reseats integral, previousError and
        `lastComputeMs` when it finds itself not running
  - [x] `update()` sets `running = true` once it has computed
  - [x] Confirm the existing `if (dt > 0.0f)` guards make the resumed tick
        proportional-only with no new special case

- [x] **Leave `setControlEnabled()` a pure config write**
  - [x] Verify it touches no PID state, so the web task never writes fields the
        SensorMonitor task owns

## Backend — setpoint validation

- [x] **Reject out-of-range setpoints in the route handler**
  - [x] In `ControlRoutes.cpp`, after the `is<float>()` check, reject values
        that are non-finite or outside `[10.0, 30.0]` with HTTP 400 and a
        `"success": false` body
  - [x] Ensure the rejection path performs no NVS write and does not call
        `setTargetTemperature()`
  - [x] Confirm `10.0` and `30.0` are accepted (inclusive bounds)

- [x] **Document the layered validators**
  - [x] Leave `SensorController::setTargetTemperature()`'s clamp in place as
        the non-HTTP fallback; add a comment naming its role
  - [x] Leave `Config::updateTargetTemperature()`'s reset-to-22 in place as the
        corrupt-NVS guard; add a comment naming its role

## Web interface

- [x] **Stepper markup and styling** (`data/control.html`)
  - [x] Replace the read-only Target `.control-item` with a `− value +` stepper
  - [x] Style the buttons for thumb-sized touch targets; keep the existing
        `.control-bar` layout from #36 intact
  - [x] Ensure rail buttons are visibly non-actionable at 10.0 and 30.0 °C

- [x] **Enable toggle** (`data/control.html`)
  - [x] Add a `.toggle-row` / `.toggle-switch` block reusing the existing
        `common.css` styles (as used three times in `settings.html`)
  - [x] Keep the three-symbol control-state indicator alongside it

- [x] **Client logic**
  - [x] Hold the setpoint in a local variable; render every tap immediately
  - [x] Clamp locally to `[10.0, 30.0]`; issue no request at the rails
  - [x] Debounce ~400 ms so a burst of taps coalesces into one POST
  - [x] Send `X-Requested-With: KlimaControl` on all three requests
  - [x] Add a pending-edit guard so `updateStatus()` skips only the setpoint and
        toggle fields while an edit is pending or in flight; all other fields
        keep updating
  - [x] On HTTP 4xx, restore the displayed setpoint from the device's reported
        `target_temperature`
  - [x] Clear the guard on both success and failure so the UI cannot deadlock

- [x] **Regenerate compressed assets**
  - [x] Run `scripts/compress_web.py` to refresh `src/generated/control_gz.h`
  - [x] Confirm the PlatformIO pre-build hook produces the same output

## Tests

- [x] **Bumpless restart** (`pio test -e native`)
  - [x] Resume after a long disabled gap: integral not saturated, output
        proportional to the small error
  - [x] Resume after a sensor-invalid gap: same behaviour, no user action
  - [x] First tick after construction: elapsed uptime not applied as `dt`
  - [x] Two consecutive running ticks: no reset, `dt` is the tick interval
  - [x] Resumed tick: `dt == 0`, integral increment zero, derivative zero
  - [x] Instances do not share PID state (the old `static` hazard)

- [ ] **Setpoint validation** — NOT covered by an automated test. Route handlers
      compile only under `ARDUINO` (`#ifdef` in `ControlRoutes.cpp`) and the repo
      has no native harness for them, so the range check was verified by reading
      the code, not by executing it. Moved to hardware verification below rather
      than claimed as tested.
  - [ ] `10.0` and `30.0` accepted; `9.9`, `30.1`, and non-finite rejected
  - [ ] Rejected request leaves `DeviceConfig.target_temperature` unchanged

- [x] **Existing suites still pass**
  - [x] `test/test_temperature_control` (updated or replaced per the decision
        above)
  - [x] `test/test_config`, `test/test_sensor_controller`

## Verification on hardware

- [ ] `pio run -e adafruit_qtpy_esp32s2`, flash, open the control page
- [ ] Stepper moves in 0.5 °C steps and holds at both rails
- [ ] A three-tap burst produces exactly one request (browser network tab)
- [ ] Setpoint survives a reboot (NVS persistence)
- [ ] Setpoint is adjustable while control is disabled
- [ ] Toggle enables/disables control and the `−`/`○`/`●` symbol tracks
      `control_active` independently of the toggle position
- [ ] E-paper footer picks up the new setpoint, rate-limited by the
      `RefreshPolicy` floor rather than one refresh per tap
- [ ] Watch `Reset reason:` across a session of setpoint changes — no
      `BROWNOUT`, keeping this clear of `assess-display-brownout-risk`
- [ ] Toggle control off and on after an hour; confirm the `PID restart:` line
      at `ESP_LOGD` shows a proportional-only, unsaturated first output
- [ ] `curl` the range check (no native coverage — see Tests above):
      `{"value": 10.0}` and `{"value": 30.0}` return 200; `9.9`, `30.1` and
      `null` return 400 and leave `target_temperature` unchanged in
      `/api/status`

## Documentation

- [x] Regenerate API docs if `scripts/generate-api-docs.sh` covers the changed
      response codes
- [x] Note the 4xx behaviour change for `POST /api/temperature/target` in the
      change's archive summary — it is a breaking change for any caller that
      relied on silent clamping

## Hardware verification: run 2026-09-02, BLOCKED

Flashed to the `Test` device (`F32EB0`, 192.168.110.243). Two pre-existing
defects surfaced; neither is caused by this change, but the second blocks it.

### Blocker 1 — NVS keys over the 15-character limit (FIXED, verified)

`Preferences::putX()` fails silently on a key longer than 15 characters
(`NVS_KEY_NAME_MAX_SIZE` is 16 incl. terminator) and the matching `getX()`
returns the supplied default. Four keys in the `ConfigManager` set were over:

| key | length |
|---|---|
| `target_temperature` | 18 |
| `temperature_control_enabled` | 27 |
| `sensor_i2c_address` | 18 |
| `energy_wifi_sleep` | 17 |

So the setpoint and the control-enable flag have **never persisted, on any
firmware version**. Nothing wrote to them until this change added a UI, which is
why it went unnoticed. `/api/status` calls `loadDeviceConfig()` on every poll,
so an in-memory change was clobbered from NVS within a second.

- [x] Shortened to `target_temp`, `ctrl_enabled`, `sensor_i2c`, `wifi_sleep`.
      No migration needed — none of them ever held data
- [x] Added `Config::nvsKeyFits()` + `static_assert` per key. A prose warning
      was already in `PrefsKeys.h` and did not prevent this, so it is now
      enforced by the compiler
- [x] Corrected the `PrefsKeys.h` comment, which stated the wrong threshold
      ("17+ characters") and described it as a soft guideline
- [x] Verified on hardware: `control_enabled` now survives repeated
      `/api/status` polls **and** a reboot. Before the fix it reverted in ~1 s

### Blocker 2 — body-carrying POSTs intermittently return 501 (NOT FIXED)

Split out into its own change: `assess-body-post-501`. Summary: any POST with a
request body returns `501 Handler did not handle the request` once the device
enters a degraded state, which is sticky until reboot. It affects untouched
routes (`/api/settings/elevation`, `/api/syslog`, `/api/settings/device-name`),
so it is pre-existing and not caused by this change. No-body POSTs and GETs are
unaffected.

It is **intermittent, not absolute** — an earlier note here said otherwise,
before the working window below was observed.

- [ ] Blocked on `assess-body-post-501`
- [ ] Re-run the whole list below once it is fixed, on a device that has been up
      long enough to have entered the degraded state at least once

### Verified on hardware

The full setpoint contract, exercised against the device with logs attached:

- [x] `10.0` and `30.0` accepted (inclusive bounds), `200`
- [x] `9.9`, `30.1`, `45`, `null`, `"abc"` rejected with `400` and
      `Value out of range 10.0-30.0`; stored setpoint unchanged
- [x] Malformed JSON rejected with `400 Invalid JSON`
- [x] Missing `X-Requested-With` rejected with `403`
- [x] Valid setpoint accepted and **persisted** across `/api/status` polls,
      which reload from NVS
- [x] Setpoint adjustable while control is disabled
- [x] Toggle enables and disables control; `control_enabled` persists across a
      reboot

**Bumpless restart confirmed on the device** (debug build,
`PLATFORMIO_BUILD_FLAGS="-DCORE_DEBUG_LEVEL=4"`):

```
enable       -> PID restart: T=24.1 (target=24.2), output=0.23 (proportional only)
+45 s        -> PID: output=0.64, I=0.42        integral accumulated
disabled 60 s
re-enable    -> PID restart: T=24.1 (target=24.2), output=0.20 (proportional only)
next tick    -> PID: output=0.23, I=0.01        integral reset, not saturated
```

- [x] Without the fix the 60 s gap would have become `dt`, adding
      `Ki·e·dt = 0.1 × 0.1 × 60 = 0.6` on top of the retained `0.42` and
      clamping to full output. Observed output on resume was `0.20`

**Soak: 30 setpoint changes at 2 s intervals with the panel enabled**

- [x] 30/30 returned `200`
- [x] E-paper refreshed 8 times over ~75 s, spaced ~10.7 s — the
      `RefreshPolicy` minimum-interval floor holding, not one refresh per
      change (which would have been 30)
- [x] Zero unexpected resets during the session, so no `BROWNOUT` under
      setpoint churn with the display active. This keeps the change clear of
      `assess-display-brownout-risk`

**Diagnostics wired up along the way**

- [x] `AccessLogger` registered (`WebServerManager` constructor). Every request
      now logs client IP, URL, method, elapsed ms and status

**Browser interaction, observed from the device log**

A human tapped the stepper in a real browser while `AccessLogger` was capturing.
Every POST moved the setpoint by exactly 1.5 °C — three taps of 0.5 °C
coalesced into one request:

```
00177927 /api/temperature/target POST (0 ms) 200   -> set to 23.0
00181312 /api/temperature/target POST (0 ms) 200   -> set to 24.5   (+1.5)
00186843 /api/temperature/target POST (0 ms) 200   -> set to 26.0   (+1.5)
00193352 /api/temperature/target POST (0 ms) 200   -> set to 27.5   (+1.5)
00216638 /api/temperature/target POST (0 ms) 200   -> set to 26.0   (-1.5)
00217870 /api/temperature/target POST (0 ms) 200   -> set to 24.5   (-1.5)
00218500 /api/temperature/target POST (0 ms) 200   -> set to 23.0   (-1.5)
00199785 /api/control/enable      POST (4 ms) 200
```

- [x] A three-tap burst produces exactly one request — confirmed from the
      device log, which is stronger evidence than the browser network tab
      because it shows what the device actually received
- [x] Stepper moves in 0.5 °C increments in a real browser, in both directions
- [x] The enable toggle works from the browser
- [x] Debounce coalesces without losing taps: the cumulative value is correct
      every time, so no tap was dropped and none was sent twice

Still outstanding:

- [ ] Rail behaviour at 10.0 and 30.0 not exercised in a browser — the session
      reached 27.5 °C. The endpoint side is verified above, and the rails are
      covered by the offline harness, but nobody has clicked `+` at 30.0
- [ ] `Reset reason:` at boot is still awkward to capture — it prints at ~2 s
      and USB CDC re-enumeration costs the first ~3.5 s after a soft reboot.
      Exposing `esp_reset_reason()` through `/api/about` would make the
      long-run brownout watch practical; noted, not done

### Correction: logging does work

An earlier note here said USB CDC serial produced no output. That was wrong —
`pio device monitor -e adafruit_qtpy_esp32s2` works. The failed attempts used
raw pyserial with default DTR/RTS and an RTS reset toggle, which re-enumerates a
native-USB-CDC port and drops the handle.

Consequences for this list:

- [ ] `Reset reason:` / no-`BROWNOUT` watch is achievable after all. The monitor
      does drop across a reboot (CDC re-enumerates), so reattach after reset
- [ ] The `PID restart:` line still needs `-DCORE_DEBUG_LEVEL=4`, because
      `ESP_LOGD` is compiled out of a default build (`Log.h:47`). That part of
      the earlier note stands

### Not verifiable as written


- [ ] "Confirm the `PID restart:` line at `ESP_LOGD`" — `CORE_DEBUG_LEVEL`
      defaults to 0 (`Log.h:47`), so `ESP_LOGD` is compiled out. Needs a build
      with `-DCORE_DEBUG_LEVEL=4`
