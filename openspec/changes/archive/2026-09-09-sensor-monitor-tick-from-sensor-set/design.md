## Context

`SensorMonitor::task()` loops: feed the task watchdog, `readSensors(now)`, `getProcessValue()`, `control.update()`, then `vTaskDelay(readingInterval - elapsed)` with `readingInterval` fixed at 1000 ms. `SensorController::readSensors()` already decides per sensor whether it is due, so most ticks do nothing. The 1 s tick exists only because the SGP40 driver requires a 1 s sample rate.

Constraints:

- `startTask()` is called after `SensorController::begin()` has registered every sensor found on the bus; the sensor set never changes afterwards. Sensor *status* does change: an `InitFailed` sensor is retried inside `readSensors()` every 30 s.
- The task is subscribed to the 30 s task watchdog, so the tick needs a healthy margin under 30 s and must keep running with no sensors fitted.
- `TemperatureController::update()` must be invoked on every tick, including while disabled, so it can mark the tick as skipped.
- `vTaskDelay(n)` returns at a tick boundary, so the real wait is in `(n-1, n]` ms and `millis()` can come up 1 ms short of the interval.

## Goals / Non-Goals

**Goals:**

- Tick at the rate the fitted sensors actually need: 15 s for an all-default device, 1 s with an SGP40.
- Decide once, at startup, with no per-tick scheduling query.
- No change to which sensor is read on which call of `readSensors()`.

**Non-Goals:**

- Changing `MEASUREMENT_INTERVAL_MS`, the default phase logic, or any sensor driver's `requiredIntervalMs()`.
- Making the Network task tick adaptive. Its idle iteration is microseconds and there is no light-sleep to gain.
- Feeding the PID between sensor reads. The process value cannot change between reads.

## Decisions

**D1: The tick is a startup constant derived from the configured sensor set, not a per-tick query.**
`SensorController::minReadIntervalMs() const` returns the minimum over all configured sensors of `requiredIntervalMs()` if non-zero, else `MEASUREMENT_INTERVAL_MS`; `MEASUREMENT_INTERVAL_MS` when nothing is configured. Alternative considered and prototyped: a `msUntilNextRead(nowMs)` query evaluated after every iteration, which self-corrects early wakes and adapts to sensors going offline. Rejected because the sensor set is fixed after `setup()`, so the adaptive machinery buys nothing the static choice does not, at the cost of a per-tick walk over slot state and a harder-to-reason-about cadence.

**D2: Status is ignored when choosing the tick.**
A sensor that failed init is retried inside `readSensors()` and must find the tick already running at its rate the moment it comes online. Excluding offline sensors would leave an SGP40 that was slow to start sampled at 15 s until a reboot.

**D3: Clamp to `[MIN_TICK_MS = 100, MAX_TICK_MS = MEASUREMENT_INTERVAL_MS]`.**
The upper bound keeps the task ticking (retries, skipped-tick bookkeeping, TWDT feed) at 15 s when nothing is fitted, with 2x margin under the 30 s watchdog; a `static_assert` ties it to `MEASUREMENT_INTERVAL_MS`. The lower bound guards against a future driver declaring an implausibly small interval.

**D4: Every sleep adds `WAKE_MARGIN_MS = 2`.**
An early RTOS wake used to slip a default read by one harmless second; at a 15 s tick it would skip the whole cycle and leave a 30 s gap. Two milliseconds of margin makes the wake strictly late. The default phase rebases to `now` on each cycle, so the drift does not accumulate into anything observable.

**D5: Remove `readingInterval` and its accessors.**
No caller outside `SensorMonitor` exists. A settable fixed interval next to a derived tick invites someone to set it and be surprised.

**D6: Diagnostics interval 5 → 15 minutes in both tasks.**
Pure log volume; the stack HWM lines remain periodic, which is what the system-architecture spec requires.

## Risks / Trade-offs

- [PID computes late by up to one tick] → With the default 60 s interval and a 15 s tick the fourth tick lands at ≥ 60 s, so the cadence is unchanged. A `control_interval_s` below the tick computes once per read; the process value could not have changed in between.
- [Autotuner / over-temperature shutoff sampled less often] → They are sampled on every read, which is exactly as often as they were effectively sampled before.
- [Work overruns the tick] → `elapsed >= tickMs` yields for one RTOS tick, as before; the default phase rebases so no double read occurs.
- [Cycle statistics in `/api/about` change scale] → They still report milliseconds slept per cycle.
