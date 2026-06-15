## Context

`OTAUpdater` currently runs two FreeRTOS tasks — `otaCheckTask` and
`otaWorkerTask` — on a **single** static stack/TCB pair (`otaTaskStack[16 KB]`
and `otaTaskTCB` in `src/OTAUpdater.h:161-162`). The shared buffer is a
deliberate work-around for a real constraint: a FreeRTOS task stack must come
from one contiguous block of *internal* SRAM (never PSRAM), and on the
ESP32-S2 such a block is scarce once WiFi/mbedTLS have fragmented the heap.
Reserving the 16 KB in BSS at link time sidesteps that fragmented runtime
heap — `xTaskCreateStatic` can never fail to allocate the buffer because the
buffer is just a global array that already exists at boot.

But sharing one buffer between two tasks means the two tasks can never run
*concurrently*. The current code enforces that with a four-part protocol:

1. `updateInProgress` (`std::atomic<bool>`, claimed via
   `compare_exchange_strong` inside `startBackgroundUpdate`) gates the
   update worker.
2. The check task refuses to start while `updateInProgress` is set (see
   `startBackgroundCheck`).
3. Each task body sets `taskParked` and calls `vTaskSuspend(nullptr)` as
   its final act, *instead* of self-deleting — because a self-deleting task
   is reaped asynchronously by the idle task, so its handle can dangle.
4. The next start path calls `reapParkedTask()`, which busy-waits on
   `taskParked.load()` and then `vTaskDelete()`s the parked task
   synchronously.

The protocol works today, but it has three friction points the design needs
to acknowledge:

- The reaper's busy-wait (`while (!taskParked.load()) vTaskDelay(2)` in
  `OTAUpdater.cpp:404`) is bounded, but it does assume the parked task is
  alive long enough to set its flag. If the parked task is killed before it
  reaches that point, the reaper spins forever.
- The "two callers are serialized on the AsyncTCP event task" precondition
  is **not** enforced by code; it is enforced by the fact that
  ESPAsyncWebServer only ever runs on the AsyncTCP task. A future refactor
  that adds a second starter (e.g. a CLI or MQTT trigger) would silently
  break the assumption and the two OTA tasks could end up racing on the
  same buffer.
- The whole arrangement exists for one reason only: to keep a single 16 KB
  buffer reusable. If each task gets its own buffer, the protocol goes away
  with no loss of functionality.

The change splits the single 16 KB buffer into two buffers (7 KB for the
check task, 12 KB for the update worker), uses a per-task `xTaskCreateStatic`
call, and lets each task self-delete on its way out. Both sizes were tuned
from on-device high-water-mark measurements with a 25% safety margin (check
actual ~5.5 KB, update actual ~9.3 KB). The 3 KB BSS cost is acceptable:
total BSS goes from 16 KB to 19 KB, well under the ~320 KB SRAM budget and
far under the 4 MB flash budget.

## Goals / Non-Goals

**Goals:**

- Make it impossible for `otaCheckTask` and `otaWorkerTask` to ever run on
  the same stack/TCB, regardless of any future change to the caller side
  of the protocol.
- Remove the custom parking/reaping protocol (`taskParked` flag,
  `vTaskSuspend` + `reapParkedTask()`) and go back to the simpler
  `vTaskDelete(nullptr)` cleanup that FreeRTOS already provides when each
  task owns its own buffers.
- Keep the public surface (`startBackgroundCheck`,
  `startBackgroundUpdate`, `startBackgroundUpdateFromLatestCheck`,
  `getCheckResult`, `performUpdate`, `isUpdateInProgress`) byte-for-byte
  identical.
- Keep the `updateInProgress` atomic flag — it is still needed to prevent
  two concurrent update workers and to drive the network task's low-heap
  guard. Only the *stack-sharing* part of the design goes away.

**Non-Goals:**

- No change to the OTA flow itself (HTTP endpoints, JSON shapes, partition
  handling, rollback, certificate verification, download streaming, progress
  callbacks) — those are part of the user-observable `ota-updates`
  capability and are unaffected.
- No change to the priority of the OTA tasks (both stay at priority 1, the
  same as the Network and Sensor Monitor tasks).
- No change to the `updateInProgress` claim protocol — it is still
  acquired via `compare_exchange_strong` inside
  `startBackgroundUpdate`.
- No change to the `CheckState` / `FirmwareInfo` / `checkResultMutex`
  machinery that hands the check result from the worker to the HTTP
  handler.
- No move of the OTA buffers out of BSS into a smaller runtime allocation.
  Internal SRAM is the constraint, and BSS is the most reliable way to
  guarantee a contiguous block.

## Decisions

### 1. Two static buffers, sized from on-device measurements

Replace the single `otaTaskStack[UPDATE_TASK_STACK]` / `otaTaskTCB` pair
with two independent pairs:

- `otaCheckStack[CHECK_TASK_STACK]` (7 KB) and `otaCheckTCB` for the
  background check.
- `otaUpdateStack[UPDATE_TASK_STACK]` (12 KB) and `otaUpdateTCB` for the
  update worker.

The sizes were chosen from on-device high-water-mark measurements with a
25% safety margin: check used ~5.5 KB (7 KB → 27% headroom), update used
~9.3 KB (12 KB → 29% headroom). The starting point during development was
8 KB / 16 KB (the values that originally fit on the ESP32-S2's fragmented
internal heap); the post-validation tuning shrank both to closer-fit values.
The `xTaskCreateStatic` calls become independent:

```cpp
otaCheckHandle = xTaskCreateStatic(
    otaCheckTask, "ota_check",
    CHECK_TASK_STACK, job, 1,
    otaCheckStack, &otaCheckTCB
);

otaUpdateHandle = xTaskCreateStatic(
    otaWorkerTask, "ota_update",
    UPDATE_TASK_STACK, job, 1,
    otaUpdateStack, &otaUpdateTCB
);
```

**Why two buffers, not one larger buffer with explicit `top` offsets:**
A single 19 KB buffer with the check task using `[0, 7K)` and the update
worker using `[0, 12K)` would also work, but it forces the two tasks to
remain mutually exclusive (you still cannot run both at once because the
update worker's stack would clobber the check task's high-water mark).
The whole point of this change is to make the tasks *capable* of running
independently — and giving them disjoint buffers is the simplest way to
encode that. The 3 KB extra BSS (over the original 16 KB shared buffer) is
cheap; the design clarity is worth it.

**Alternatives considered:**

- *Keep one buffer, drop the parking protocol, accept that two concurrent
  OTA tasks would corrupt each other's stacks.* — Rejected. This is
  exactly the failure mode the change is meant to eliminate.
- *Allocate the stack at runtime via `xTaskCreate` instead of static.* —
  Rejected. This was the original arrangement before the static buffer
  was added; the `errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY` failure on a
  fragmented internal heap is what forced the move to BSS in the first
  place (see the comment block in `src/OTAUpdater.h:142-160`). We are
  *not* giving up the static-allocation guarantee.
- *Move the OTA tasks off FreeRTOS onto the AsyncTCP event task itself.*
  — Rejected. The original design notes explain why OTA is not on the
  AsyncTCP task: a multi-minute TLS download would block every other HTTP
  request.

### 2. Drop the parking protocol; let each task self-delete

The `taskParked` flag, the `vTaskSuspend(nullptr)` exit path, and the
`reapParkedTask()` function are removed. Both task bodies return to the
simpler `vTaskDelete(nullptr)` final action, which is the standard
FreeRTOS idiom for a task that owns its own buffers and is the only user
of them.

The `updateInProgress` flag still gates concurrent updates (it is acquired
via `compare_exchange_strong` in `startBackgroundUpdate` before either
buffer is touched), so the protocol that *actually* matters — only one
update worker at a time — is unchanged. What goes away is the reaper and
the busy-wait, which were only there to keep a single buffer reusable.

The `otaTaskHandle` field is replaced by two locals — one inside
`startBackgroundCheck`, one inside `startBackgroundUpdate` — because each
start path now owns its own handle for the lifetime of the task. We do
not need to keep the handle around after `xTaskCreateStatic` returns,
because the task self-deletes on exit and nothing else needs to refer to
it.

**Why not keep a global `otaTaskHandle` for symmetry with the existing
code?** The existing global is only there because the reaper needs to
`vTaskDelete()` the *previous* task. With each task self-deleting, there
is no previous task to delete, and the global becomes a dangling pointer
waiting to happen. A local is honest about the lifetime.

**Alternative considered:** Keep the global and have the next start
`vTaskDelete()` the *currently running* task. — Rejected. The handle
becomes stale the instant the task self-deletes, and racing the
self-delete is the exact TOCTOU window we are trying to close.

### 3. Keep `updateInProgress` exactly as it is

The `std::atomic<bool> updateInProgress` flag, its
`compare_exchange_strong` claim in `startBackgroundUpdate`, and its
`load()` in `isUpdateInProgress` and the network task's low-heap guard
are unchanged. The flag is the *correct* part of the protocol: it is
sequentially consistent on the S2, and it gates the only race that
actually matters (two concurrent update workers).

The reaper's busy-wait on `taskParked` was a defense-in-depth that
existed to keep one buffer reusable. With one buffer per task, it has
no purpose.

**Alternative considered:** Drop `updateInProgress` too, on the grounds
that two concurrent updates are now physically impossible. — Rejected.
The flag is also used as a "OTA in progress" signal to the network
task's low-heap guard (see `OTAUpdater.cpp:113-117` for the analogous
usage in `checkForUpdate`). It is a *visibility* flag, not just a
*mutex* flag, and the visibility value is what tells the rest of the
system "do not reboot, we are in the middle of a flash." That
visibility is independent of the stack-sharing question.

## Risks / Trade-offs

- **BSS grows by 3 KB (16 KB → 19 KB) after tuning.** → Internal SRAM is
  ~320 KB and the firmware is currently using ~75 KB. 3 KB more is ~1 % of
  the budget and well under any realistic headroom concern. Verified by
  reading the AGENTS.md budget section; the `.pio/build/*/firmware.elf`
  size after the change can be cross-checked with
  `pio run -e adafruit_qtpy_esp32s2` + `size`.

- **Two static TCBs and two static stacks in BSS make the symbol surface
  slightly larger.** → This is a source-level concern only; the linker
  treats both buffers as ordinary `.bss`, so there is no
  `ld`-fragility risk. The names are file-scope `static inline` so they
  do not appear in the linker symbol table at all.

- **The change is a private refactor with no new test surface.** → The
  `native` test environment does not run FreeRTOS tasks, so there is no
  way to write a unit test that exercises the new buffer ownership on
  the host. The test that *does* matter is "build the firmware for the
  S2, reflash the device, run an OTA check, run an OTA update, watch
  the device reboot into the new partition." This is a manual
  validation step, called out as the last task in `tasks.md`.

- **A future regression that re-introduces a shared buffer would not be
  caught by a test.** → The new spec requirement ("each OTA task owns
  its own static stack and TCB") is a review-time invariant, not a
  runtime check. If a future change wants to share buffers again, the
  reviewer has to explicitly justify it against the spec, which is the
  intended friction.

- **Self-deleting tasks are still a FreeRTOS footgun for anyone who
  holds a stale handle.** → In this design, the only code that ever
  holds the handle is the start function that just called
  `xTaskCreateStatic`, and that handle is dropped on return. Nothing
  outside the start function ever reads it. The footgun is
  architecturally impossible to step on here.

## Migration Plan

This is a source-level refactor with no data migration, no config
schema change, and no firmware-image change beyond the recompile.

1. Apply the edits in `tasks.md` (header split, parking removal, task
   body cleanup, comment update).
2. Build the firmware for the S2:
   `pio run -e adafruit_qtpy_esp32s2`.
   Verify the firmware size is within budget and the symbol table
   contains the new buffer names (`otaCheckStack`, `otaUpdateStack`,
   `otaCheckTCB`, `otaUpdateTCB`) and *does not* contain
   `otaTaskStack` or `otaTaskTCB`.
3. Build the native test environment:
   `pio test -e native`.
   The native build does not exercise the FreeRTOS task code (it is
   guarded by `#ifdef ARDUINO`), so this is a smoke test that the
   refactor did not accidentally break a non-`ARDUINO` code path.
4. Flash the firmware and run a full OTA cycle: trigger a check, then
   an update, then `POST /api/ota/confirm` after the new partition
   boots cleanly. This is the only validation that exercises the
   actual stack/TCB ownership.

**Rollback:** `git revert` the change. The previous parking protocol
is a strict superset of the new self-delete model (it works with
either shared or unshared buffers), so a revert restores the old
behaviour without any data loss. The on-device NVS state, the partition
contents, and the running firmware are unaffected by this change in
either direction.
