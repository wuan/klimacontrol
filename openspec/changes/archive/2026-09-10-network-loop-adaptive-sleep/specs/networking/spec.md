## ADDED Requirements

### Requirement: Network task sleeps adaptively based on previous iteration's work

The Network task SHALL sleep between iterations for a duration computed
from the previous iteration's work. The task SHALL define a constant
`TICK_MS = 15000` and a constant `WAKE_MARGIN_MS = 2` (matching
`Task::SensorMonitor::WAKE_MARGIN_MS`). At the top of each iteration the
task SHALL compute `sleepMs = (lastWorkMs < TICK_MS) ? (TICK_MS -
lastWorkMs + WAKE_MARGIN_MS) : 1u`, where `lastWorkMs` is the integer
millisecond duration of the previous iteration's work (the value
already computed as `workMs = blockExit - now` and recorded via
`stats.add(workMs)` per the requirement "Network loop accumulates
per-iteration work-duration stats"), and SHALL then call
`vTaskDelay(pdMS_TO_TICKS(sleepMs))`. On the first iteration
(`lastWorkMs == 0`) the sleep SHALL be `TICK_MS + WAKE_MARGIN_MS = 15002`
ms, matching the Sensor Monitor's first-iteration behaviour.

The rationale (mirroring the existing Sensor Monitor requirement at
`openspec/specs/sensor-management/spec.md:336`) is that a fast iteration
should return to sleep promptly without burning the full 15 s budget,
while a slow iteration should re-enter the work loop as soon as the
RTOS schedules it so a transient stall (MQTT blocking, NTP hung, mDNS
re-init) self-corrects on the next tick instead of waiting out a fixed
15 s. The `WAKE_MARGIN_MS` constant ensures the wake is strictly later
than the nominal interval so an early RTOS wake cannot leave the next
iteration's work short of its 15 s budget.

#### Scenario: First iteration sleeps for TICK_MS + WAKE_MARGIN_MS

- **WHEN** the Network task starts and no previous iteration has run
  (`lastWorkMs == 0`)
- **THEN** the task SHALL sleep for `15000 + 2 = 15002` ms before its
  first iteration's work, matching the current behaviour of
  `vTaskDelay(15000)` within the 2 ms margin

#### Scenario: Fast iteration shortens the next sleep

- **WHEN** the previous iteration's work took 50 ms
- **THEN** the next sleep SHALL be `15000 - 50 + 2 = 14952` ms, so the
  total cycle is `50 + 14952 = 15002` ms (the 2 ms margin) instead of
  the previous fixed `50 + 15000 = 15050` ms

#### Scenario: Slow iteration yields the floor of one RTOS tick

- **WHEN** the previous iteration's work took 16000 ms (longer than
  `TICK_MS`)
- **THEN** the next sleep SHALL be `1` ms (`pdMS_TO_TICKS(1)`), so the
  task re-enters the work loop on the next RTOS tick instead of waiting
  out a full additional 15 s

#### Scenario: 15 s cadence is preserved on average

- **WHEN** every iteration's work takes `W` ms where `W < TICK_MS`
- **THEN** each iteration's total cycle SHALL be `W + (TICK_MS - W +
  WAKE_MARGIN_MS) = TICK_MS + WAKE_MARGIN_MS`, so the long-run average
  cadence is the 15 s tick plus the 2 ms margin, matching the Sensor
  Monitor's contract