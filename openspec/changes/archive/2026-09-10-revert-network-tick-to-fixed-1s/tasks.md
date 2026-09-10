## 1. Code

- [x] 1.1 `src/Network.cpp`: remove `TICK_MS_COARSE` and the
      `statusLed.isDark()` tick selection; sleep
      `TICK_MS_FINE - lastWorkMs + WAKE_MARGIN_MS` only when
      `lastWorkMs < TICK_MS_FINE`, otherwise do not delay.
- [x] 1.2 `src/Network.cpp`: drop the `coarse_ticks ||` term from the
      MQTT publish condition so publishes follow the configured
      interval only.

## 2. Spec

- [x] 2.1 Rewrite the networking requirement "Network task sleeps
      adaptively based on previous iteration's work" for a fixed 1 s
      tick and remove the dark-mode scenarios.
- [x] 2.2 `openspec validate --all --strict` passes.
