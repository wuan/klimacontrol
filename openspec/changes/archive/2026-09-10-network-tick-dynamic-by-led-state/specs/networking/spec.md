## MODIFIED Requirements

### Requirement: Network task sleeps adaptively based on previous iteration's work

The Network task SHALL sleep between iterations for a duration computed
from the previous iteration's work. At the top of each iteration the
task SHALL first compute the current tick from the LED's dark state as
`tickMs = statusLed.isDark(static_cast<uint32_t>(millis())) ? 15000 : 1000`,
where `statusLed` is the `DarkModeStatusLed` instance wired into the
class at construction and `isDark()` returns `true` exactly when dark
mode is actively suppressing the LED (the ON/TRANSMIT_DATA flash is
held dark and the NeoPixel rail is cut). The task SHALL then compute
`sleepMs = (lastWorkMs < tickMs) ? (tickMs - lastWorkMs + WAKE_MARGIN_MS)
: 1u`, where `lastWorkMs` is the integer millisecond duration of the
previous iteration's work (the value already computed as `workMs =
blockExit - now` and recorded via `stats.add(workMs)` per the
requirement "Network loop accumulates per-iteration work-duration
stats") and `WAKE_MARGIN_MS = 2` (matching
`Task::SensorMonitor::WAKE_MARGIN_MS`), and SHALL then call
`vTaskDelay(pdMS_TO_TICKS(sleepMs))`. On the first iteration
(`lastWorkMs == 0`) the sleep SHALL be `tickMs + WAKE_MARGIN_MS = 1002`
ms or `15002` ms depending on the LED state at boot.

The rationale (mirroring the existing Sensor Monitor requirement at
`openspec/specs/sensor-management/spec.md:336`) is that a fast iteration
should return to sleep promptly without burning the full budget, while
a slow iteration should re-enter the work loop as soon as the RTOS
schedules it so a transient stall (MQTT blocking, NTP hung, mDNS
re-init) self-corrects on the next tick instead of waiting out a fixed
interval. The `WAKE_MARGIN_MS` constant ensures the wake is strictly
later than the nominal interval so an early RTOS wake cannot leave the
next iteration's work short of its tick budget. The dynamic `tickMs`
ensures the budget itself matches the device's current state: 1 s
while the LED is visibly indicating something (user is likely watching),
15 s once the LED has decided nobody is watching and gone dark.

#### Scenario: First iteration sleeps for tickMs + WAKE_MARGIN_MS while LED is active

- **WHEN** the Network task starts and no previous iteration has run
  (`lastWorkMs == 0`) AND the LED is not in dark mode
  (`statusLed.isDark(now)` returns `false`)
- **THEN** the task SHALL sleep for `1000 + 2 = 1002` ms before its
  first iteration's work, matching the previous `vTaskDelay(1000)`
  behaviour within the 2 ms margin

#### Scenario: First iteration sleeps for tickMs + WAKE_MARGIN_MS while LED is dark

- **WHEN** the Network task starts and no previous iteration has run
  (`lastWorkMs == 0`) AND the LED has already entered dark mode
  (`statusLed.isDark(now)` returns `true`)
- **THEN** the task SHALL sleep for `15000 + 2 = 15002` ms before its
  first iteration's work

#### Scenario: Fast iteration shortens the next sleep

- **WHEN** the previous iteration's work took 50 ms AND the LED is
  not in dark mode (`tickMs == 1000`)
- **THEN** the next sleep SHALL be `1000 - 50 + 2 = 952` ms, so the
  total cycle is `50 + 952 = 1002` ms (the 2 ms margin)

#### Scenario: Fast iteration at dark-mode tick shortens the next sleep

- **WHEN** the previous iteration's work took 50 ms AND the LED is
  in dark mode (`tickMs == 15000`)
- **THEN** the next sleep SHALL be `15000 - 50 + 2 = 14952` ms, so
  the total cycle is `50 + 14952 = 15002` ms (the 2 ms margin)

#### Scenario: Slow iteration yields the floor of one RTOS tick

- **WHEN** the previous iteration's work took longer than the current
  `tickMs` (e.g. 1200 ms while `tickMs == 1000`, or 16000 ms while
  `tickMs == 15000`)
- **THEN** the next sleep SHALL be `1` ms (`pdMS_TO_TICKS(1)`), so
  the task re-enters the work loop on the next RTOS tick instead of
  waiting out a full additional tick

#### Scenario: Tick switches when LED transitions to dark

- **WHEN** iteration N ran with `tickMs == 1000` AND during iteration
  N's work the LED enters dark mode (so iteration N+1's `isDark()`
  call returns `true`)
- **THEN** iteration N+1 SHALL compute `tickMs == 15000` and sleep
  for the dark-mode budget (minus `lastWorkMs` plus `WAKE_MARGIN_MS`)

#### Scenario: Tick switches when LED transitions out of dark

- **WHEN** iteration N ran with `tickMs == 15000` AND during iteration
  N's work the LED is re-activated (so iteration N+1's `isDark()`
  call returns `false`)
- **THEN** iteration N+1 SHALL compute `tickMs == 1000` and sleep
  for the active-mode budget (minus `lastWorkMs` plus `WAKE_MARGIN_MS`)

#### Scenario: Long-run cadence is preserved on average

- **WHEN** every iteration's work takes `W` ms where `W < tickMs`
- **THEN** each iteration's total cycle SHALL be `W + (tickMs - W +
  WAKE_MARGIN_MS) = tickMs + WAKE_MARGIN_MS`, so the long-run average
  cadence is the current tick plus the 2 ms margin, matching the
  Sensor Monitor's contract