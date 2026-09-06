## Context

`Task::SensorMonitor` ticks every 1000 ms and calls `SensorController::readSensors()` followed by `updateControl()`. `readSensors()` takes the I2C bus lock, calls `read()` on every `Online` sensor in dependency-sorted order, accumulates the results into a fresh vector, and replaces `currentMeasurements` wholesale under `dataMutex`. `dataValid` is true iff at least one sensor returned valid data on that tick.

Consumers:

- `updateControl()` reads `getTemperature()` every tick but only computes the PID every `control_interval_s` (default 60 s), measuring elapsed time rather than counting ticks.
- MQTT publishes `getValidMeasurements()` every `MqttConfig::interval` (default 15 s), gated on `isDataValid()`.
- `SensorRoutes`, `StatusRoutes` and `DisplayManager` read `getSnapshot()` or `isDataValid()` on demand.

The `SGP40` driver depends on `Temperature` and `RelativeHumidity` appearing in the `prior` vector, which today is the current tick's accumulator. `sortSensors()` orders providers before consumers so this works within a single tick. The `SCD4x` driver keeps a private `co2` value because its hardware produces data every 5 s and the 1 Hz poll would otherwise report "invalid" four ticks out of five.

Constraints:

- Single-core ESP32-S2; `readSensors()` runs on the SensorMonitor task, readers run on the AsyncTCP and Network tasks. `currentMeasurements` is swapped under `dataMutex`.
- `reserveSensorSlots(n)` must keep guaranteeing that `currentMeasurements` never reallocates for up to `n * MAX_MEASUREMENTS_PER_SENSOR` measurements (`memory-management` spec).
- The native `millis()` stub in `src/support/Timer.cpp` is wall-clock based, so scheduling logic must be testable with an injected time.
- Sensirion's VOC index algorithm, as wrapped by `Adafruit_SGP40::measureVocIndex()`, assumes it is fed once per second.

## Goals / Non-Goals

**Goals:**

- Read each sensor only as often as it needs, with a per-driver declared interval and a 15 s system default.
- Keep all default-interval sensors on the same tick so cross-sensor derived values (dew point, SGP40 compensation) are computed from time-coherent inputs.
- Keep the SensorMonitor tick, `updateControl()`, the HTTP API and the MQTT payload unchanged.
- Preserve the existing dependency ordering so a dependent sensor always sees its inputs, even when those inputs were last read several ticks ago.
- Keep `isDataValid()` meaningful for the MQTT gate and `updateControl()`.

**Non-Goals:**

- Making the measurement interval user-configurable.
- Exposing per-measurement age or timestamps to the API or MQTT.
- Non-blocking (trigger/collect) reads for slow sensors such as the BME680. The cache introduced here is a prerequisite, but that is a separate change.
- Changing the SensorMonitor tick period or making it sleep until the next due sensor.

## Decisions

### D1. Interval is declared by the driver as a single `uint32_t`

`Sensor::Sensor` gains `[[nodiscard]] virtual uint32_t requiredIntervalMs() const { return 0; }`. Zero means "no requirement, use the system measurement interval". Only `SGP40` overrides it, returning 1000.

*Alternatives considered:* a `Cadence {Fixed|Minimum|SelfPaced, periodMs}` descriptor was considered to distinguish "exactly N" (SGP40) from "no faster than N" (BME680 heater). With a 15 s default the "no faster than" floors never bind, so the distinction buys nothing today. A user-configurable per-sensor period in the `44=SHT4x` assignment string was rejected: the datasheet knowledge belongs next to `providesMeasurements()` in the driver.

### D2. Default-interval sensors share one system phase, not per-sensor timers

`readSensors()` computes one boolean per tick, `defaultDue = (now - lastDefaultCycleMs >= MEASUREMENT_INTERVAL_MS)`, and reads every default-interval sensor when it is true. Sensors with a non-zero `requiredIntervalMs()` keep their own `lastReadMs` and are due when `now - lastReadMs >= requiredIntervalMs()`. On the very first cycle, and whenever `lastDefaultCycleMs` has never been set, `defaultDue` is true, so the boot-time read is unchanged.

*Why:* a per-sensor timer would drift default sensors apart the moment one of them failed its first `begin()` and came online through the 30 s retry path, breaking the "same tick" guarantee and giving the SGP40 a temperature from one phase and a humidity from another. A single shared phase makes coherence structural rather than incidental.

The tick is 1000 ms and the interval is 15 000 ms, so `defaultDue` is true on every 15th tick with no accumulated drift (`lastDefaultCycleMs` is set to `now`, not advanced by the interval, so a late tick simply shifts the phase rather than causing a double read).

### D3. Per-sensor cache slots, snapshot is their union

`SensorController` holds `std::vector<SensorSlot> slots` parallel to `sensors` (same index, maintained by `addSensor()` and `sortSensors()`). Each slot carries:

- `std::vector<Sensor::Measurement> measurements` — the sensor's last valid reading plus its `Time` measurement;
- `uint32_t lastValidMs` — when that reading was taken;
- `uint32_t lastReadMs` — when `read()` was last attempted (used for the D2 per-sensor due check);
- `bool valid`.

After the read phase, `readSensors()` builds `allMeasurements` by concatenating the `measurements` of every valid slot in sensor order, then swaps it into `currentMeasurements` under `dataMutex` exactly as today. Because slots are concatenated in the sorted sensor order, the existing "first sensor that reported the type wins" rule for `getTemperature()` and friends is preserved.

*Why a parallel vector rather than a member on `Sensor`:* the cache is a controller concern (scheduling and publication). Drivers stay stateless with respect to time, and the `Sensor` interface does not grow storage that only one consumer uses. *Why not keep `currentMeasurements` as the cache and patch it in place:* the union has to be rebuilt anyway when a slot is invalidated (D5), and rebuilding into a fresh vector keeps the single-swap-under-mutex publication pattern that readers already rely on.

### D4. `prior` is the union of slots, built before the read loop

Before reading any sensor, `readSensors()` builds the union of currently valid slots into `allMeasurements`. Each sensor that reads this tick is passed that vector as `prior`. After a sensor's read succeeds, its slot is replaced, and the entries for that sensor in `allMeasurements` are replaced too, so a later sensor in the same tick sees the fresh value from an earlier one. On a tick where only the SGP40 is due, `prior` simply contains the 15 s old SHT4x values, which is what humidity compensation wants.

Concretely, this is easiest as: build `prior` from slots at the top; run the read loop updating slots; rebuild the union from slots at the bottom. The intermediate "replace entries in place" step can be avoided by rebuilding `prior` from slots before each dependent read; with at most ~10 sensors this is negligible on the SensorMonitor task.

### D5. Slot invalidation: status leaves `Online`, or the reading ages out

A slot is cleared (`valid = false`, `measurements.clear()`) when either:

- the sensor's `SensorStatus` is no longer `Online` (it went `ReadFailing` or `InitFailed`), or
- `now - lastValidMs > 3 × effectiveInterval` where `effectiveInterval` is the sensor's requirement or the system default.

The age bound exists because the `READ_FAILURE_THRESHOLD` of 10 consecutive failures used to mean 10 s at 1 Hz; at 15 s it would mean 150 s of a stale temperature feeding `updateControl()`. Three missed intervals (45 s default, 3 s for the SGP40) bounds staleness independently of the failure counter.

`dataValid` is true iff at least one slot is valid after invalidation. `lastReadingTimestamp` and `lastReadingTime` are set to `now` on any tick in which at least one sensor read succeeded. `getTimeSinceLastReading()` therefore reports time since the last successful read of *any* sensor, which for a system with an SGP40 is always under a second and otherwise steps in 15 s increments. That is acceptable for its one consumer, the `/api/status` diagnostics field.

### D6. I2C recovery counts attempted sensors only

The existing streak logic becomes: `anyI2CAttempted` (an I2C sensor was due and `Online` this tick) and `anyI2CValid`. The streak increments only when `anyI2CAttempted && !anyI2CValid`, resets on `anyI2CValid`, and is untouched on ticks where no I2C sensor was attempted. Without this, a system whose only 1 Hz sensor has failed would see 14 "no valid I2C reading" ticks per cycle and trigger bus recovery every few seconds.

### D7. `SCD4x` loses its private `co2` cache

`SCD4x::read()` returns `valid = true` with a CO2 measurement only when `getDataReadyStatus()` reports fresh data and `readMeasurement()` succeeds; otherwise `valid = false`. The controller slot now performs the role the private field played. The SCD4x default is periodic mode with a 5 s period, so at a 15 s read interval there is always fresh data waiting, and the "not ready" branch becomes rare rather than the common case.

One consequence: a "not ready" read is now recorded by `recordReadResult(false)` and counts toward `ReadFailing`. At 15 s reads with a 5 s sensor period that requires 10 consecutive missed data-ready flags, i.e. the sensor has actually stopped producing, so this is the correct signal.

### D8. `readSensors()` takes the clock as a parameter

`readSensors()` becomes a thin wrapper around `readSensors(uint32_t nowMs)`, mirroring how `PidController::update()` takes its clock. Native tests drive the scheduling, cache and invalidation logic with explicit timestamps instead of sleeping against the wall-clock `millis()` stub.

### D9. The constant lives with the other sensor constants

`static constexpr uint32_t MEASUREMENT_INTERVAL_MS = 15000;` in `SensorController` (public, so tests can reference it). It is intentionally not in `Config.h`: the proposal rules out a configuration knob, and the SensorMonitor comment in `SensorController.h` that says the reading interval "is itself settable" refers to the task tick, which remains as it is.

## Risks / Trade-offs

- [Measurements are now up to 15 s old when read] → This is by construction, and all consumers already tolerate it: the PID default interval is 60 s, MQTT publishes every 15 s, the display refresh floor is well above 15 s. The autotuner reads temperature every tick; its relay-oscillation period on a heating plant is minutes, so a 15 s sample period is well within what it needs. Documented in the spec so a future faster consumer knows to check.

- [Capacity contract for `currentMeasurements`] → The union is bounded by `sensors.size() × MAX_MEASUREMENTS_PER_SENSOR` exactly as before, since each slot holds one sensor's reading plus its `Time` entry. The slot vectors themselves are new heap allocations, one small vector per sensor, allocated once at `addSensor()` and reused; `reserveSensorSlots()` reserves `MAX_MEASUREMENTS_PER_SENSOR` per slot so they never grow at runtime.

- [A default-interval sensor that comes online mid-cycle waits up to 15 s for its first read] → Acceptable. Alternatively read it immediately on its first `Online` tick and let it re-align on the next shared phase; this is a one-line refinement if the delay is noticed in practice. Not doing it initially keeps D2 simple.

- [SGP40 with no humidity sensor present] → Unchanged from today: `prior` has no temperature, `measureVocIndex` is not called, the reading is invalid and the sensor eventually goes `ReadFailing`. `sortSensors()` already warns about unmet dependencies.

- [BME680 gas resistance quality] → Bosch recommends a 3 s heater cycle for the IAQ profile. At 15 s the gas resistance baseline behaves differently from the 1 Hz baseline users may have seen in MQTT history. It is raw resistance, not a computed index, so nothing in the firmware depends on its absolute level.

- [`Time` measurement semantics] → Still "duration of the last `read()`", but now it is refreshed only when the sensor is read, so consumers watching it in MQTT see it hold for 15 s. It was always a diagnostic; no logic depends on it.

## Migration Plan

Firmware-only change, no persisted state, no API shape change. Deploy as a normal OTA. Rollback is a normal OTA to the previous release. Observable difference after deploy: the SensorMonitor cycle-delay stats in `/api/about` will show shorter per-tick work on 14 of 15 ticks, and the BME680 temperature should read slightly lower once heater self-heating stops.

## Open Questions

None blocking. Two follow-ups deliberately deferred:

- Whether to expose the per-slot `lastValidMs` as an age in the API for diagnostics.
- Whether newer Sensirion gas-index algorithm versions allow a configurable sampling interval, which would let the SGP40 join the default phase and remove the last 1 Hz requirement.
