## 1. Add adaptive-sleep state to `Network`

- [x] 1.1 In `src/Network.h`, add a private `uint32_t lastWorkMs = 0;`
      member alongside `lastBlockExitMs` and `lastActuatorTickMs` with
      a comment explaining that it carries the previous iteration's
      work duration across the `vTaskDelay` boundary for the adaptive
      sleep

## 2. Replace fixed sleep with adaptive sleep in `Network::task()`

- [x] 2.1 In `src/Network.cpp`, inside `Network::task()`'s `while (true)`
      loop body, declare `static constexpr uint32_t TICK_MS = 15000;`
      and `static constexpr uint32_t WAKE_MARGIN_MS = 2;` alongside
      the other `static constexpr` constants (e.g. next to
      `MIN_FREE_INTERNAL_BYTES`)
- [x] 2.2 Replace the existing `vTaskDelay(15000 / portTICK_PERIOD_MS);`
      at the top of the loop with the adaptive sleep: compute
      `sleepMs = (lastWorkMs < TICK_MS) ? (TICK_MS - lastWorkMs +
      WAKE_MARGIN_MS) : 1u;` and call
      `vTaskDelay(pdMS_TO_TICKS(sleepMs));`
- [x] 2.3 At the bottom of the loop, right after
      `stats.add(workMs);`, add `lastWorkMs = static_cast<uint32_t>(workMs);`
      so the next iteration's sleep uses this iteration's work
      duration

## 2. Replace fixed sleep with adaptive sleep in `Network::task()`

- [x] 2.1 In `src/Network.cpp`, inside `Network::task()`'s `while (true)`
      loop body, declare `static constexpr uint32_t TICK_MS = 15000;`
      and `static constexpr uint32_t WAKE_MARGIN_MS = 2;` alongside
      the other `static constexpr` constants (e.g. next to
      `MIN_FREE_INTERNAL_BYTES`)
- [x] 2.2 Replace the existing `vTaskDelay(15000 / portTICK_PERIOD_MS);`
      at the top of the loop with the adaptive sleep: compute
      `sleepMs = (lastWorkMs < TICK_MS) ? (TICK_MS - lastWorkMs +
      WAKE_MARGIN_MS) : 1u;` and call
      `vTaskDelay(pdMS_TO_TICKS(sleepMs));`
- [x] 2.3 At the bottom of the loop, right after
      `stats.add(workMs);`, add `lastWorkMs = static_cast<uint32_t>(workMs);`
      so the next iteration's sleep uses this iteration's work
      duration

## 3. Validate

- [x] 3.1 Run `openspec validate --all --strict` from the repo root
      and confirm the new delta spec passes
- [x] 3.2 Run `pio test -e native` and confirm the native test build
      still compiles and all tests pass (the change is device-only)
- [x] 3.3 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the
      firmware build succeeds