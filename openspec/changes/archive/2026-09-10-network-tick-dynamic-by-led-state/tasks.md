## 1. Replace `TICK_MS` constant with LED-aware computation

- [x] 1.1 In `src/Network.cpp`, inside `Network::task()`'s `while (true)`
      loop body, delete the `static constexpr uint32_t TICK_MS = 15000;`
      line and replace it with a runtime computation at the top of each
      iteration:
      ```cpp
      const uint32_t tickMs = statusLed.isDark(static_cast<uint32_t>(millis())) ? 15000 : 1000;
      ```
      Place it immediately before the existing `const uint32_t sleepMs = ...`
      computation so the new tick drives the same formula.
- [x] 1.2 Keep `static constexpr uint32_t WAKE_MARGIN_MS = 2;` exactly as
      it is.

## 2. Validate

- [x] 2.1 Run `openspec validate --all --strict` from the repo root
      and confirm the modified delta passes
- [x] 2.2 Run `pio test -e native` and confirm the native test build
      still compiles and all tests pass (the change is device-only)
- [x] 2.3 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the
      firmware build succeeds