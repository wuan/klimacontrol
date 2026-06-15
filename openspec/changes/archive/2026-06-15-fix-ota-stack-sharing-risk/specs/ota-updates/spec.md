## ADDED Requirements

### Requirement: OTA task stack isolation

The firmware SHALL give the background check task (`otaCheckTask`) and the update worker task (`otaWorkerTask`) one statically-reserved FreeRTOS stack and one Task Control Block (TCB) each. The two tasks SHALL release their own buffers on exit, and the firmware SHALL NOT share a single static stack/TCB pair between them. The `updateInProgress` atomic flag SHALL still gate concurrent update workers, and the `xTaskCreateStatic` calls SHALL still use statically-reserved buffers (linker BSS) so that task creation cannot fail on a fragmented runtime internal-SRAM heap.

#### Scenario: Check task is created with its own stack and TCB

- **WHEN** `OTAUpdater::startBackgroundCheck()` calls `xTaskCreateStatic` to spawn the background check task
- **THEN** the call SHALL pass a buffer pair (stack + TCB) that is dedicated to the check task and is not shared with the update worker task

#### Scenario: Update worker is created with its own stack and TCB

- **WHEN** `OTAUpdater::startBackgroundUpdate()` calls `xTaskCreateStatic` to spawn the update worker task
- **THEN** the call SHALL pass a buffer pair (stack + TCB) that is dedicated to the update worker and is not shared with the check task

#### Scenario: Each task self-deletes on exit

- **WHEN** either the check task or the update worker task finishes its work (success or failure)
- **THEN** the task SHALL release its own stack/TCB by calling `vTaskDelete(nullptr)` as its final action, and no other task SHALL be responsible for reaping it

#### Scenario: No shared static stack/TCB remains in the firmware

- **WHEN** the source is read
- **THEN** there SHALL be no declaration of a single shared `otaTaskStack` array or `otaTaskTCB` `StaticTask_t` used by both task bodies, and the firmware SHALL declare a separate stack array and TCB per task
