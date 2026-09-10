## MODIFIED Requirements

### Requirement: Network task sleeps adaptively based on previous iteration's work

The Network task SHALL run on a fixed nominal tick of `TICK_MS_FINE =
1000` ms. The tick SHALL NOT depend on the status LED's dark state or on
any other runtime condition; an earlier revision stretched it to 15 s
while the LED was dark, and that was withdrawn because the work the loop
services (MQTT keepalive and reconnect, NTP retry, WiFi state checks)
is designed around a 1 s cadence and gained nothing from the longer
sleep on a mains-powered device.

At the top of each iteration, when the previous iteration's work took
less than the tick (`lastWorkMs < 1000`), the task SHALL sleep for
`1000 - lastWorkMs + WAKE_MARGIN_MS` via `vTaskDelay(pdMS_TO_TICKS(...))`,
where `lastWorkMs` is the integer millisecond duration of the previous
iteration's work (the value already computed as `workMs = blockExit -
now` and recorded via `stats.add(workMs)` per the requirement "Network
loop accumulates per-iteration work-duration stats") and
`WAKE_MARGIN_MS = 2` (matching `Task::SensorMonitor::WAKE_MARGIN_MS`).
When the previous iteration's work took `1000` ms or longer, the task
SHALL NOT delay at all and SHALL proceed directly to the next
iteration's work. On the first iteration (`lastWorkMs == 0`) the sleep
SHALL be `1000 + WAKE_MARGIN_MS = 1002` ms.

The rationale (mirroring the Sensor Monitor requirement at
`openspec/specs/sensor-management/spec.md:336`) is that a fast iteration
should return to sleep promptly without burning the full budget, while
a slow iteration should re-enter the work loop as soon as possible so a
transient stall (MQTT blocking, NTP hung, mDNS re-init) self-corrects on
the next pass instead of waiting out a fixed interval. `WAKE_MARGIN_MS`
makes the wake strictly later than the nominal interval so an early RTOS
wake cannot leave the next iteration's work short of its tick budget.

MQTT publishing within the loop SHALL be gated only by the configured
publish interval (`now - lastMqttPublish >= intervalMs`), the 60 s
post-boot settle period, and data validity. The tick cadence SHALL NOT
by itself trigger a publish.

#### Scenario: First iteration sleeps for tick plus margin

- **WHEN** the Network task starts and no previous iteration has run
  (`lastWorkMs == 0`)
- **THEN** the task SHALL sleep for `1000 + 2 = 1002` ms before its
  first iteration's work, regardless of whether the status LED is dark

#### Scenario: Fast iteration shortens the next sleep

- **WHEN** the previous iteration's work took 50 ms
- **THEN** the next sleep SHALL be `1000 - 50 + 2 = 952` ms, so the
  total cycle is `50 + 952 = 1002` ms (the 2 ms margin)

#### Scenario: Slow iteration re-enters the loop without sleeping

- **WHEN** the previous iteration's work took 1000 ms or longer
  (e.g. 1200 ms)
- **THEN** the task SHALL NOT call `vTaskDelay` and SHALL begin the next
  iteration's work immediately, so the stall is not extended by a full
  additional tick

#### Scenario: Tick is unchanged by LED dark mode

- **WHEN** the status LED's dark mode engages or releases between
  iterations
- **THEN** the tick used for the sleep computation SHALL remain
  `1000` ms; `DarkModeStatusLed::isDark()` SHALL NOT influence it

#### Scenario: Long-run cadence is preserved on average

- **WHEN** every iteration's work takes `W` ms where `W < 1000`
- **THEN** each iteration's total cycle SHALL be `W + (1000 - W +
  WAKE_MARGIN_MS) = 1002` ms, so the long-run average cadence is one
  tick plus the 2 ms margin, matching the Sensor Monitor's contract

#### Scenario: MQTT publish follows the configured interval only

- **WHEN** the broker is connected, the device has been up for at least
  60 s, sensor data is valid, and fewer than `intervalMs` milliseconds
  have elapsed since the last publish
- **THEN** no publish SHALL occur on that iteration, irrespective of the
  status LED's dark state
