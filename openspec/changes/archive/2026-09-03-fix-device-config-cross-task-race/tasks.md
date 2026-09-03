# Tasks: cross-task `DeviceConfig` reads are torn

Ordered so each step is independently testable in `native` and the change is
useful after section 3 (the lock + snapshot, which is the heart of the fix).
Sections 4 and 5 migrate the three call sites the design identifies.

## 1. Spec scaffolding

- [x] 1.1 Verify `openspec validate fix-device-config-cross-task-race --strict`
      passes (artifacts already exist)
- [x] 1.2 Re-read `openspec/changes/fix-device-config-cross-task-race/proposal.md`
      and `design.md` before touching code, so the decisions are fresh

## 2. Lock and snapshot accessor on `ConfigManager`

- [x] 2.1 In `src/Config.h`, add a `mutable std::atomic_flag deviceConfigLock`
      next to the existing `restartLock`, initialised with `ATOMIC_FLAG_INIT`
      in the default member initialiser (mirrors the existing pattern)
- [x] 2.2 Add private `void lockDeviceConfig() const` and
      `void unlockDeviceConfig() const` methods next to `lockRestart()` /
      `unlockRestart()` — same `test_and_set(acquire)` / `clear(release)`
      shape, with a comment citing `restartLock` as the precedent
- [x] 2.3 Add public `Config::DeviceConfig getDeviceConfigSnapshot() const`
      that takes the lock, copies the struct, releases; place it next to
      `getDeviceConfig()` and add a comment explaining why the const-reference
      accessor still exists (same-task readers)
- [x] 2.4 In `src/Config.cpp`, add the corresponding definitions. NRVO/RVO
      should collapse the return-by-value into a single lock-copy-unlock
      sequence; verify by inspecting the produced assembly if a future build
      ever complains

## 3. Wrap every `updateXxx()` cache write

- [x] 3.1 In `src/Config.cpp`, take the lock around the cache write in each
      of the eight methods that touch `deviceConfig`. The NVS write stays
      outside the lock (design D2). Methods to update:
      `updateDeviceName`, `updateTargetTemperature`,
      `updateTemperatureControlEnabled`, `updateActuatorAssignment`,
      `updateActuatorTiming`, `updateTuning`, `updateElevation`,
      `updateTimezone`, `updateSensorI2CAddress`
- [x] 3.2 `saveDeviceConfig()`: lock around the single
      `deviceConfig = validated` assignment, not around each field
- [x] 3.3 `loadDeviceConfig()`: lock around the full field-by-field
      initialisation, because every field is written. The NVS reads stay
      outside the lock (single-writer at boot, before tasks exist)
- [x] 3.4 Build the firmware with `pio run -e adafruit_qtpy_esp32s2` to
      catch a forgotten method or a typo in the locking — the change is
      mechanical but touches every `updateXxx()` and a missed one would
      re-introduce the race

## 4. `updateControl()` reads the snapshot

- [x] 4.1 In `src/SensorController.cpp::updateControl()`, replace the five
      `config.getDeviceConfig().xxx` reads with a single
      `const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();`
      at the top, and switch each reference to `cfg.xxx`. Verify by reading
      the resulting function that every field used by `updateControl()`
      comes from `cfg`
- [x] 4.2 In `src/SensorController.cpp::isHeatingPermitted()`, replace
      `config.getDeviceConfig().temperature_control_enabled` with the
      snapshot (small additional fix; design D5)

## 5. Network task snapshot

- [x] 5.1 In `src/Network.cpp` line 586, replace
      `heatingActuator.configure(config.getDeviceConfig())` with a local
      snapshot:
      ```cpp
      const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();
      heatingActuator.configure(cfg);
      ```
      Design D5: `configure()` reads `actuator_host` and `actuator_channel`
      together and the pair is only meaningful as a whole.

## 6. Native multi-threaded test

- [x] 6.1 Create `test/test_device_config_snapshot/test_device_config_snapshot.cpp`
      with the `pio test` Unity harness (matches `test_pid_gain_requests` and
      `test_config_restart_atomic` as templates)
- [x] 6.2 Add `+<test/test_device_config_snapshot/>` to the `native` env
      `build_src_filter` in `platformio.ini` so the suite is picked up
      (review point #8 — the current filter silently drops about half the
      suites; this change only needs to add one)
- [x] 6.3 Test: a single writer thread running `updateSafetyMax` /
      `updateSafetyHyst` round-trips, a single reader thread calling
      `getDeviceConfigSnapshot()` in a tight loop. For every snapshot, assert
      that the (safety_max, safety_hyst) pair is either (old, old),
      (new, new), or a (new, old) tuple that never existed in the
      writer's sequence — *never* (old, new). That last case is exactly the
      torn observation the fix removes
- [x] 6.4 Test: two writers and two readers, to stress the lock
- [x] 6.5 Test: a snapshot taken *during* a long NVS-style delay (synthetic
      sleep inside a `updateXxx()` body is not present in production but can
      be simulated by holding the lock from a side channel in the test) does
      not block readers indefinitely — bounded by the actual NVS write time.
      Skip if the test setup does not allow it; the property is enforced by
      design (lock only around the cache write) and is hard to exercise
      without mocking NVS
- [x] 6.6 `pio test -e native` — all green, including the existing gains
      and config-restart-atomic suites (regression check)

## 7. Verify and close out

- [x] 7.1 `pio test -e native` — all green
- [x] 7.2 `pio run -e adafruit_qtpy_esp32s2` — builds and reports the same
      flash / RAM headroom as the baseline (no new allocations on the hot
      path)
- [x] 7.3 Re-read `src/SensorController.cpp::updateControl()` end-to-end
      and confirm every `cfg.xxx` reference corresponds to a real field used
      by the function, with no accidental drop
- [x] 7.4 Update `docs/CODE_REVIEW.md` finding #5 to mark it resolved, in
      the shape of the existing entries (point at the change name and the
      files touched, link to the change under `openspec/changes/archive/`
      after archive)
- [x] 7.5 `/opsx:archive`
