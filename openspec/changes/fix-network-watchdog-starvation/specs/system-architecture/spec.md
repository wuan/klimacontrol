## MODIFIED Requirements

### Requirement: FreeRTOS task structure

The firmware SHALL use FreeRTOS with two long-running tasks: a Network task running `Network::task()` and a Sensor Monitor task running `Task::SensorMonitor::task()`. Both tasks SHALL be registered with the ESP-IDF task watchdog using a 30-second timeout, and watchdog timeout SHALL trigger a panic. Each task body SHALL call `esp_task_wdt_reset()` at least once per iteration AND, in addition, SHALL feed the watchdog before and after any blocking external call that may exceed the per-iteration budget (see the *Network task blocking-call safety* requirement in `networking` for the Network task's specific obligations).

#### Scenario: Tasks register with watchdog

- **WHEN** each task body starts
- **THEN** `esp_task_wdt_add(NULL)` is called and `esp_task_wdt_reset()` is called at least once per iteration

#### Scenario: Network task stack size

- **WHEN** the Network task is created via `xTaskCreate`
- **THEN** its stack is at least 14 KB (per current configuration; the original 10 KB target was raised for stability)

#### Scenario: Sensor Monitor task stack size

- **WHEN** the Sensor Monitor task is created
- **THEN** its stack is at least 8 KB

#### Scenario: Blocking external call feeds the watchdog

- **WHEN** a task body makes a blocking external call (e.g. a UDP exchange in the Network task) that may take longer than the per-iteration watchdog budget
- **THEN** the task body feeds `esp_task_wdt_reset()` immediately before and immediately after the call, so a hung call does not starve the 30 s task watchdog
