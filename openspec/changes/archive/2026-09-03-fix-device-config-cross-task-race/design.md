## Context

`ConfigManager` owns the in-memory cache of every persisted config struct. Each
`updateXxx()` method follows the same shape:

1. Validate the input (fall back to defaults).
2. Write to NVS (under `PreferencesGuard`).
3. `deviceConfig.field = value;` — the in-memory cache.

Step 3 is currently unlocked, which is fine when the only readers are the same
task that writes (the AsyncTCP web task, since route handlers run there).

Three other tasks now read from the same cache, and at least one of them reads
more than one field at a time:

| reader | path | fields read |
|---|---|---|
| Sensor Monitor task | `SensorController::updateControl()` | `safety_max_c`, `safety_hyst_c`, `target_temperature`, `temperature_control_enabled`, `control_interval_s` |
| Sensor Monitor / web task | `SensorController::isHeatingPermitted()` | `temperature_control_enabled` |
| Network task | `Network.cpp:586` → `HeatingActuator::configure()` | `actuator_host`, `actuator_channel` |

The review's example of the bug is the over-temperature shutoff:

```
  web task                      Sensor Monitor task
  ─────────                     ─────────────────────
  updateSafetyMax(35.0f) ──►    [reading cfg.safety_max_c]
  updateSafetyHyst(2.0f) ──►    ...
                                cfg.safety_max_c    = 35.0f   (new)
                                cfg.safety_hyst_c   = 1.0f    (old)
                                ...
                                t > cfg.safety_max_c - cfg.safety_hyst_c?
                                ⇒ engages shutoff with new limit, old hysteresis
```

Same shape applies to `target_temperature` + `control_interval_s`, where the
PID can charge its integral against a `dt` measured under the old interval but
with the new target on the next computing tick. None of these are caught today
because the failure mode is one wrong tick out of many.

The codebase already serializes its other cross-task state — `dataMutex` in
`SensorController`, `restartLock` in `ConfigManager`, the gains request flag —
but `deviceConfig` slipped through because it predates the multi-field
control-loop reader and the existing readers added fields incrementally.

## Goals / Non-Goals

**Goals:**

- A reader of `DeviceConfig` from a different task than the writer gets a
  single internally consistent snapshot.
- The fix fits the project's existing concurrency style (spinlock over the
  `restartLock` precedent), not a new abstraction.
- Cross-task readers move to the snapshot in this change: `updateControl()`,
  `isHeatingPermitted()`, `HeatingActuator::configure()`.
- A native test exercises the snapshot under concurrent writers and readers
  and asserts no torn observation.

**Non-Goals:**

- A wholesale rewrite of `ConfigManager` to be lock-everywhere. Single-field
  readers (e.g. `isControlEnabled()`, `getTargetTemperature()`) are not part
  of this race and continue to use `getDeviceConfig()` directly.
- Mutex coverage for `WifiConfig`, `MqttConfig`, `EnergyConfig`, `SyslogConfig`,
  `DisplayConfig`, `SensorConfig`. None of these have cross-task multi-field
  readers today. Worth a follow-up audit; not this change.
- Changing the gains-pattern path. It already works.

## Decisions

### D1 — Spinlock in `ConfigManager`, matching `restartLock`

```cpp
mutable std::atomic_flag deviceConfigLock = ATOMIC_FLAG_INIT;

void lockDeviceConfig() const {
    while (deviceConfigLock.test_and_set(std::memory_order_acquire)) {
        // spin
    }
}
void unlockDeviceConfig() const {
    deviceConfigLock.clear(std::memory_order_release);
}

DeviceConfig getDeviceConfigSnapshot() const {
    lockDeviceConfig();
    DeviceConfig copy = deviceConfig;
    unlockDeviceConfig();
    return copy;
}
```

- The lock protects the cache only. NVS writes stay inside `PreferencesGuard`,
  which is its own serialization point and already slower than the spinlock by
  three orders of magnitude.
- Per-tick cost on the consumer: two atomic ops + one ~250-byte struct copy.
  At 1 Hz, that is invisible. A freeRTOS mutex would cost the same in steady
  state but heavier in the contended case; the codebase's existing answer for
  short critical sections is `std::atomic_flag` (`restartLock` is precedent),
  so this stays consistent.
- Same producer on every code path: the AsyncTCP web task. The lock also
  serializes producers if a future code path adds one, which it would not
  otherwise need to think about.

*Alternatives considered:*

- **`std::atomic<DeviceConfig>`** — `DeviceConfig` is 16+ bytes and not
  lock-free on the Xtensa target (the same issue the project worked around
  for the restart state with `packRestartState` + `restartLock`).
- **Gains-pattern shadow + flag in `SensorController`** — the reviewer
  suggested this. It would be cheaper on the steady state (one atomic
  exchange per tick instead of two atomic ops + a copy), but it has two
  problems for this case: (a) the consumer must read the snapshot on *every*
  tick, not just when the flag is set, so the cheap-path benefit is mostly
  lost; and (b) the same struct has two other cross-task readers
  (`isHeatingPermitted()`, the actuator configure call), so the
  single-producer/single-consumer model the gains pattern exploits does not
  apply. Centralising the synchronisation in `ConfigManager` lets one
  mechanism cover all three readers.
- **FreeRTOS mutex** — heavier, and inconsistent with the existing spinlock
  precedent.

### D2 — Lock only around the cache write, not the NVS write

```cpp
void ConfigManager::updateTargetTemperature(float temperature) {
    // ... validate ...
#ifdef ARDUINO
    PreferencesGuard guard(prefs, NAMESPACE, false);
    guard.get().putFloat(TARGET_TEMPERATURE, temperature);
#endif
    lockDeviceConfig();
    deviceConfig.target_temperature = temperature;
    unlockDeviceConfig();
}
```

Putting the lock around the NVS write as well would block a Sensor Monitor
snapshot for tens of milliseconds on every settings change. The cache is the
shared state the consumers actually read; the NVS write is single-writer and
already serialized by `PreferencesGuard`. Lock only the cache.

### D3 — `loadDeviceConfig()` and `saveDeviceConfig()` lock around the bulk assignment

These two methods overwrite multiple fields at once, so the lock spans the
whole assignment, not each field:

```cpp
void ConfigManager::saveDeviceConfig(const DeviceConfig &config) {
    DeviceConfig validated = config;
    validateDeviceConfig(validated);
#ifdef ARDUINO
    PreferencesGuard guard(prefs, NAMESPACE, false);
    guard.get().putString("device_name", validated.device_name);
    // ... other putX calls ...
#endif
    lockDeviceConfig();
    deviceConfig = validated;
    unlockDeviceConfig();
}
```

The intermediate `validated` local is a plain struct copy outside the lock —
fine because nothing is reading it yet. The lock spans only the publish step
into `deviceConfig`.

### D4 — Snapshot at the top of `updateControl()`

```cpp
float SensorController::updateControl() {
    const uint32_t now = millis();

    // Single, consistent view of every config field this tick needs.
    // Without this, the tick could see the new target but the old safety
    // limit, or vice versa.
    const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();

    // ... rest of updateControl(), every `config.getDeviceConfig().xxx`
    //     replaced with `cfg.xxx`
}
```

The snapshot is one `DeviceConfig` copy (~250 bytes) per tick. NRVO/RVO
collapses the return-by-value into a single lock-copy-unlock sequence.

### D5 — Two other cross-task readers migrate to the snapshot too

`isHeatingPermitted()` reads `temperature_control_enabled` (one field) and
`SensorController::safetyShutoff` (a local). The deviceConfig read is
single-field, but it is still cross-task. Adopting the snapshot is one line
and removes a small remaining race.

`Network.cpp:586` passes `config.getDeviceConfig()` to
`HeatingActuator::configure()`, which reads `actuator_host` and
`actuator_channel` together (the assignment is only meaningful as a pair —
the channel is `-1` unless the host is non-empty). This is the same shape of
race as the over-temperature shutoff, just at a 30-second cadence.

Both move to `config.getDeviceConfigSnapshot()`.

### D6 — The `const DeviceConfig&` accessor stays for same-task readers

`getDeviceConfig()` continues to return `const DeviceConfig&` because the
existing route handlers on the AsyncTCP task read from the same task that
writes — no race, no copy needed, and route handlers use multiple fields
(they would not benefit from a snapshot if they did not have one). The
snapshot is purely additive.

## Risks / Trade-offs

- **A 250-byte copy per tick.** At 1 Hz on a Sensor Monitor task that already
  does 1-second work, the copy is unmeasurable. The atomic-flag
  test-and-set + clear is single-digit nanoseconds. → Confirmed by reading
  the existing `restartLock` precedent, which spins on the same primitive in
  a hotter path.
- **Lock contention with NVS.** A `updateXxx()` call holds the spinlock only
  for the cache write, not the NVS write (D2). A snapshot taken during a
  NVS write proceeds normally; the cache is not being touched.
- **Lock is on a hot member for the consumer.** Every `updateControl()` tick
  takes the lock. The lock holder is the same task that was about to release
  it (because writers are on the AsyncTCP task, not the Sensor Monitor
  task), so uncontended case spins zero times. → Worst case is one
  `test_and_set` returning non-zero (the lazy-snapshot-acquire pattern is
  not used here because the data set is too large to take that risk — see
  the gains-pattern analysis in the proposal).
- **The lock does not protect the NVS read in `loadDeviceConfig()`.** That
  runs once in `setup()` before any task exists; not a cross-task concern.
- **Other cross-task readers introduced later would also need the snapshot.**
  Mitigated by the spec requirement that any new cross-task reader use
  `getDeviceConfigSnapshot()`. A code-review checklist note is the cheapest
  way to enforce it; CI does not lint this.
- **`HeatingActuator::configure()` takes by `const&`, so the snapshot binds
  to a local before the call.** A small extra variable at the call site;
  negligible cost.

## Migration Plan

No data migration. The on-disk format and the runtime cache shape are
unchanged. A device running the new firmware behaves identically to one
running the old one, except that a single tick of `updateControl()` that
coincides with a settings change is now correct instead of an arbitrary mix.

Rollback is a firmware downgrade; nothing persists between versions.

## Open Questions

- **Is the snapshot's 250-byte copy cheap enough to expand to *every*
  cross-task reader, or should it stay scoped to the multi-field ones?**
  Leaning toward "every" — once the infrastructure exists, the cost of
  taking it is identical and the consistency guarantee is stronger. Will
  see what the code review for the open PR says; defer until then.
- **Should the snapshot accessor live on `ConfigManager` (D1) or on a
  dedicated `DeviceConfigView` type?** The former matches the existing
  `getDeviceConfig()` location and the existing per-struct accessor
  pattern in `ConfigManager`; the latter would be more idiomatic for the
  rest of the codebase but adds a header. Defer until a second
  cross-task config struct exists.
