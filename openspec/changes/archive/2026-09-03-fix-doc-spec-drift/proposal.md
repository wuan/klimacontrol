## Why

`docs/CODE_REVIEW.md` findings **#10**, **#11**, **#12**, **#13** are
documentation/spec drift: every claim is testably wrong against the
current code, and because OpenSpec validation is self-consistent
("the spec says what the spec says"), nothing in CI catches the gap.
The four are cheap to fix together, so they ride alongside one
`fix-doc-spec-drift` change:

- **#10** — `openspec/specs/system-architecture/spec.md` requires the
  Network task stack to be **≥ 14 KB** and the SensorMonitor task stack
  to be **≥ 8 KB**. The code creates them at **8192 B** and **6144 B**
  (`src/Network.cpp:993`, `src/task/SensorMonitor.cpp:33`), each
  justified by a measured peak + headroom comment and a periodic
  `stack HWM` diagnostic. The values being lowered are right; the
  spec thresholds are stale.
- **#11** — `AGENTS.md`, `README.md`, and
  `openspec/specs/system-architecture/spec.md` all state that the
  Adafruit QT Py ESP32-S2 board has *no PSRAM*. `src/Network.cpp:555`
  records the on-device value `psram_size 2094735` (~2 MB). Only the
  task-stack requirement (internal-only) survives the correction —
  the board really does have ~2 MB of PSRAM.
- **#12** — `AGENTS.md:265-279` documents a `LedState` API that does
  not exist (`MEASURING`, `BLINK_SLOW`, `PULSE`, `setMeasuring()`,
  `setNormal()`). The shipped enum at `src/StatusLed.h:17-23` is
  `OFF, ON, STARTUP, TRANSMIT_DATA, ERROR`. The LED-behavior summary at
  `AGENTS.md:504-508` (Yellow = measuring, Blue = AP, Red = error) is
  also fiction: there is no AP state and `ON` is a green→red gradient
  driven by MQTT publish progress, not "green = normal".
- **#13** — `openspec/specs/system-architecture/spec.md:91` asserts
  "the `Network` instance SHALL own ... the `WebServerManager`
  instance". The code (`src/main.cpp:137`,
  `src/Network.h:145-168`) constructs `WebServerManager` once at file
  scope, hands `Network` a non-owning `WebServerManager*`, and
  reuses it across mode transitions. `memory-management/spec.md:11-43`
  documents this exact pattern correctly.

The fix is "pick the truth the code already encodes" everywhere; no
source files under `src/` change, no build output changes, no test
output changes — only spec and `AGENTS.md` / `README.md` prose.

## What Changes

- **`openspec/specs/system-architecture/spec.md`**
  - *FreeRTOS task structure* — lower the `Network` task stack
    threshold from "at least 14 KB" to "at least 6 KB" and the
    `SensorMonitor` task stack threshold from "at least 8 KB" to "at
    least 4 KB". Each `THEN` clause records the in-tree value
    (8192 / 6144) and the HWM-justification comment
    (`src/Network.cpp:976-989`, `src/task/SensorMonitor.cpp:22-29`)
    in the spec scenario body, so a future reviewer can verify the
    spec against the code without consulting Git history.
  - *Target hardware platform* — drop "PSRAM SHALL NOT be assumed
    available". Replace with "the board exposes ~2 MB PSRAM
    (`psram_size ≈ 2094735` on the production batch); see the
    *Memory budget* requirement for the constraint that task
    stacks and other FreeRTOS / WiFi / mbedTLS working structures
    remain internal-SRAM-only."
  - *Ownership hierarchy* — change "The `Network` instance SHALL
    own ... the `WebServerManager` instance" to "The `Network`
    instance SHALL hold a non-owning pointer to the `WebServerManager`
    constructed once in `main.cpp`; ownership of `WebServerManager`
    lives with `main.cpp`'s file-scope object". The `WHEN`/`THEN`
    scenario at lines 100-102 ("the previous `WebServerManager` is
    destroyed by resetting the `std::unique_ptr`") is rewritten to
    describe the actual `setMode(...)` call that switches the
    existing instance between `CONFIG` and `OPERATIONAL` route sets.

- **`openspec/specs/system-architecture/spec.md` — *Memory budget***
  — add a sentence recording that OTA memory accounting uses
  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` because the
  structures OTA contends with (task stacks, lwIP, AsyncTCP, DMA
  buffers, mbedTLS) are internal-only even when PSRAM is healthy.
  This is the load-bearing reason the spec values stay low even
  with PSRAM available.

- **`AGENTS.md`**
  - Remove the **No PSRAM** bullet at line 492; replace with a
    bullet stating the real shape (`~320 KB internal SRAM + ~2 MB
    PSRAM`) and pointing at `src/Network.cpp:546-555` for the
    on-device verification. Keep the "task stacks must remain
    internal-only" caveat that the existing `Memory requirements`
    paragraph (line 460) already covers.
  - In the *Known Constraints* section, lower the recorded "Sensor
    Monitor task has 8 KB stack, Network task has 10 KB stack" (line
    495) to the actual `6 KB` / `8 KB` values. (The prior numbers
    were stale predates the HWM-driven resize.)
  - Replace the *Status LED Control* section (lines 265-279) with
    documentation that matches the shipped `LedState` enum at
    `src/StatusLed.h:17-23` (`OFF, ON, STARTUP, TRANSMIT_DATA,
    ERROR`) and the methods that exist (`on()`, `off()`, `toggle()`,
    `setProgress(float)`).
  - Rewrite the LED color summary at lines 504-508 ("Green/Yellow/Blue/
    Red") to reflect what `src/StatusLed.cpp` actually does:
    `ON` is a green→red gradient driven by MQTT publish progress;
    `STARTUP` is a slow blue blink; `TRANSMIT_DATA` is a brief
    white flash; `ERROR` is solid red.

- **`README.md`**
  - Line 28 — drop `(no PSRAM)`; document ~320 KB internal + ~2 MB
    PSRAM.
  - Lines 28-37 — the RAM row stays "~320 KB" for the *internal*
    pool and gains a PSRAM row. The "Stack size" sentence at line
    376 is updated to the actual 8 KB / 6 KB values.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `system-architecture`:
  - *Target hardware platform* — PSRAM claim corrected to acknowledge
    the ~2 MB PSRAM present on the board; PSRAM *availability* and
    the *internal-SRAM-only* constraint become two separate facts.
  - *Memory budget* — note that the OTA gate's internal-only
    accounting is what keeps the spec's stack values low even with
    PSRAM available.
  - *FreeRTOS task structure* — Network task stack threshold 14 KB →
    6 KB; SensorMonitor task stack threshold 8 KB → 4 KB. Current
    8192 / 6144 values are recorded in the `THEN` clauses so a
    next reader can verify without a code diff.
  - *Ownership hierarchy* — WebServerManager ownership corrected
    from "Network owns" to "main.cpp owns, Network holds a
    non-owning pointer". The mode-flip scenario at lines 100-102
    is rewritten to describe `setMode(...)` instead of
    `reset` / `make_unique`.

No code in `src/` or `test/` is touched; no new files are added.

## Impact

- **Spec files**: `openspec/specs/system-architecture/spec.md` (four
  requirements touched, no removals).
- **Docs**: `AGENTS.md` (4 paragraphs / bullets), `README.md`
  (3 locations).
- **Source code**: none.
- **Tests**: none required. The change is itself documentation —
  the verification command is `openspec validate --all --strict`
  after folding, which currently passes on the *baseline* spec
  (the drift is invisible to the validator).
- **No web, no NVS schema, no hardware, no ABI change.**
- **Not blocked.** Independent of every other pending change; all
  four findings are pure documentation corrections against shipped
  behaviour.
