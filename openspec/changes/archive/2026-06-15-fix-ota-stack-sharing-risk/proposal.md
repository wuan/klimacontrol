## Why

`OTAUpdater` runs two FreeRTOS tasks on a **single** static stack/TCB pair:
`otaTaskStack[UPDATE_TASK_STACK]` and `otaTaskTCB` in `src/OTAUpdater.h:161-162`
are shared by `otaCheckTask` (8 KB deep) and `otaWorkerTask` (16 KB deep).
Mutual exclusion is enforced by `updateInProgress` (atomic, claimed inside
`startBackgroundUpdate`) plus a custom `taskParked` flag and a synchronous
`reapParkedTask()` reaper: when an OTA task finishes, it sets `taskParked` and
parks itself with `vTaskSuspend(nullptr)`; the next start spins on the flag and
then `vTaskDelete()`s the parked task before reusing the buffers.

The buffer-sharing itself is a defense-in-depth measure: a FreeRTOS task stack
must come from one contiguous block of *internal* SRAM (never PSRAM), and on the
ESP32-S2 such a block is scarce once WiFi/mbedTLS have fragmented the heap.
Reserving the 16 KB in BSS at link time sidesteps the fragmented runtime heap —
but it forces the two tasks to *serialize* on that one block.

The risk is that the entire correctness of the design rests on the atomic
check-and-set + park + reap dance never racing:

- A `std::atomic<bool>` store/load on the S2 is sequentially consistent, so
  the flag itself is not the weak link.
- But the surrounding flow has more moving parts: `startBackgroundCheck` and
  `startBackgroundUpdate` are both called from the AsyncTCP event task, and
  each one reaps the parked task **before** claiming the slot. If two callers
  were ever to interleave (today they cannot, but the code is a busy-loop
  spin in the reaper — `while (!taskParked.load()) vTaskDelay(2)`, see
  `OTAUpdater.cpp:404`), or if a future caller path is added that skips the
  busy-check, two tasks would end up running on the same stack/TCB — an
  immediate stack-corruption crash with no recovery.

The fix is to give each OTA task its own static stack/TCB. The 8 KB / 16 KB
sizes already exist as named constants (`CHECK_TASK_STACK`, `UPDATE_TASK_STACK`);
only the *buffer* needs to be split. BSS cost is +8 KB on top of the existing
16 KB (the check task was already using only the first half of the shared
buffer), and we delete the parking/reaping machinery entirely — fewer moving
parts, no atomic protocol to reason about, and a self-deleting task is once
again safe (`vTaskDelete(NULL)` reclaims the buffers asynchronously via the
idle task, which is fine because the buffers are no longer shared).

## What Changes

- Replace the single `otaTaskStack[UPDATE_TASK_STACK]` and `otaTaskTCB` static
  buffers in `src/OTAUpdater.h` with two independent pairs: `otaCheckStack` /
  `otaCheckTCB` (7 KB) and `otaUpdateStack` / `otaUpdateTCB` (12 KB), each
  tuned from on-device high-water-mark measurements with a 25% safety margin
  (check actual ~5.5 KB, update actual ~9.3 KB).
- Pass the per-task buffer to its `xTaskCreateStatic` call: `otaCheckStack` /
  `&otaCheckTCB` in `startBackgroundCheck`, `otaUpdateStack` / `&otaUpdateTCB`
  in `startBackgroundUpdate`. Drop the shared `otaTaskHandle` field — each
  start path now holds a local `TaskHandle_t` it owns.
- Switch both task bodies (`otaCheckTask`, `otaWorkerTask`) back to
  `vTaskDelete(nullptr)` for cleanup. The parking protocol (`taskParked` flag,
  `vTaskSuspend`, `reapParkedTask()`) is removed.
- Remove the now-unused `otaTaskHandle`, `taskParked`, and `reapParkedTask()`
  declarations/definitions from `src/OTAUpdater.h` and `src/OTAUpdater.cpp`.
- The "park instead of self-delete" comments on both task bodies are replaced
  with a short note pointing at the new self-delete + private-stack model.
- BSS usage grows by 3 KB (from 16 KB to 19 KB total). RAM budget is ~320 KB
  with current usage ~75 KB, so 3 KB more is well within range.

No public API, no HTTP endpoint, no JSON shape, and no observable user-facing
behavior changes.

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `ota-updates`: add a new **OTA task stack isolation** requirement — the
  background check task and the update worker task SHALL each own an
  independent static stack and TCB, and SHALL release their own buffers on
  exit. The previous "one shared static stack/TCB, reaped synchronously by
  the next starter" arrangement is removed because the parking/reaping dance
  is a non-essential defense-in-depth measure whose only remaining purpose
  is to keep a single buffer reusable; giving each task its own buffer
  removes the need for it.

## Impact

- **Code**
  - `src/OTAUpdater.h` — replace `otaTaskTCB` / `otaTaskStack` with the two
    per-task pairs; drop `otaTaskHandle`, `taskParked`, and the
    `reapParkedTask()` declaration. Update the explanatory comment block
    on the static buffers to describe the new independent-stack model.
  - `src/OTAUpdater.cpp` — `startBackgroundCheck` uses the check buffer /
    TCB and stores the handle in a local variable; `startBackgroundUpdate`
    uses the update buffer / TCB and likewise stores the handle locally.
    Both task bodies call `vTaskDelete(nullptr)` as their final action.
    `reapParkedTask()` is deleted.
- **APIs**: no signature changes. `startBackgroundCheck`,
  `startBackgroundUpdate`, `startBackgroundUpdateFromLatestCheck`,
  `getCheckResult`, `performUpdate`, `isUpdateInProgress`, and the rest of
  the public surface are unchanged.
- **Behavior**: no observable change. The check and update tasks still run
  at priority 1, still call the same public entry points, and still log the
  high-water mark on exit. The atomic `updateInProgress` flag still gates
  concurrent updates exactly as before.
- **Memory**: +3 KB of BSS (16 KB → 19 KB) after tuning each buffer to
  `actual_usage × 1.25`, rounded up to the nearest 1 KB. Internal SRAM budget
  is still well within range (~320 KB available, ~75 KB used after this
  change).
- **Tests**: no new test required — the change is a private refactor and
  the existing `pio test -e native` suite still covers the non-`ARDUINO`
  code paths. The `native` build does not exercise FreeRTOS tasks, so no
  test build changes are needed.
- **Dependencies**: none. All symbols used (`StaticTask_t`, `StackType_t`,
  `xTaskCreateStatic`, `vTaskDelete`) are already in use elsewhere in the
  translation unit.
