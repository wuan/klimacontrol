## 1. Render net_* keys in the Device Info page's Network section

- [x] 1.1 In `data/about.html`, extend the `networkHTML` string
      assembly inside `loadInfo()` so each of the three branches
      (connected, AP, transition) appends four `createRow(...)` calls
      for the `net_*` keys. Use labels "Network Loop Cycle Count",
      "Network Loop Avg Work", "Network Loop Min Work", "Network Loop
      Max Work". Format `net_cycle_count` as a bare integer and the
      three `*_work_ms` values with ` ms` suffix, matching the
      Statistics section's convention for the Sensor Monitor's
      `cycle_*` keys.

## 2. Validate

- [x] 2.1 Run `openspec validate --all --strict` from the repo root
      and confirm the new delta passes
- [x] 2.2 Run `pio test -e native` and confirm the native test build
      still compiles (the change is in a `data/*.html` asset that is
      not exercised by native tests, but the change must not break
      the host build)
- [x] 2.3 Run `pio run -e adafruit_qtpy_esp32s2` and confirm the
      firmware build succeeds — this also exercises the
      `scripts/compress_web.py` pre-build hook that regenerates
      `src/generated/*_gz.h` from the edited `data/about.html`