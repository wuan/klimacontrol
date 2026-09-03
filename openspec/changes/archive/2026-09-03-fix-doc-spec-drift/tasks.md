# Tasks: documentation / spec drift corrections

This is a documentation-only change. There are no `src/` or `test/`
edits and no new spec files; the spec delta in
`openspec/changes/fix-doc-spec-drift/specs/system-architecture/spec.md`
folds into `openspec/specs/system-architecture/spec.md` via
`/opsx:archive` (the canonical spec does *not* get edited in this
phase).

The work that the implementer does directly is:

- prose edits to `AGENTS.md` (Status LED section, color summary,
  Known Constraints PSRAM and stack-size lines) — findings #10, #12;
- prose edits to `README.md` (hardware table, RAM row, Constraints,
  stack-size line) — findings #10, #11;
- prose edits to `docs/CODE_REVIEW.md` to mark findings #10, #11,
  #12, **Resolved** in the shape of the existing entries;
- verification: `openspec validate --all --strict`,
  `pio test -e native`, `pio run -e adafruit_qtpy_esp32s2`;
- `/opsx:archive` to fold the spec delta into
  `openspec/specs/system-architecture/spec.md` and move the change
  to `openspec/changes/archive/`.

## 1. Spec scaffolding

- [x] 1.1 `openspec validate fix-doc-spec-drift --strict` returns
      clean (already done when the change was proposed)
- [x] 1.2 Re-read `openspec/changes/fix-doc-spec-drift/proposal.md`
      and `design.md` before touching the docs, so the four
      corrected requirements and the chosen in-tree values are
      fresh

## 2. Update `AGENTS.md` — Status LED Control section (#12)

- [x] 2.1 In `AGENTS.md:265-279` (*Status LED Control*), replace
      the four `LedState::*` examples (`MEASURING`, `BLINK_SLOW`,
      `PULSE`, `setMeasuring()`, `setNormal()`) with the shipped
      enum values (`OFF, ON, STARTUP, TRANSMIT_DATA, ERROR` per
      `src/StatusLed.h:17-23`) and the methods the class actually
      exposes (`on()`, `off()`, `toggle()`, `setProgress(float)`,
      `getProgress()`, `getState()`, `setState(LedState)`, `update()`).
- [x] 2.2 In `AGENTS.md:504-508` (LED color summary at the bottom
      of *Device Naming*), rewrite the Green / Yellow / Blue /
      Red paragraph to the shipped mapping: `OFF` is dark; `ON`
      is a green→red gradient driven by MQTT publish progress;
      `STARTUP` is a slow blink (the comment at line 51 of
      `src/StatusLed.cpp` calls this `0x00000f`, near-black dark
      blue rather than the bright blue the original docs implied);
      `TRANSMIT_DATA` is a brief white flash (`0x020202`); `ERROR`
      is solid red (`0x0F0000`).

## 3. Update `AGENTS.md` — Known Constraints (#10, #11)

- [x] 3.1 At `AGENTS.md:492` (the first *Known Constraints*
      bullet), replace "**No PSRAM**: Adafruit QT Py ESP32-S2
      board has no PSRAM" with the corrected shape:
      "**PSRAM**: ~2 MB on board (verified on-device at
      `src/Network.cpp:555`); OTA / WiFi / mbedTLS / task-stack
      allocations remain internal-SRAM-only per the *Memory
      requirements* paragraph above (`AGENTS.md:460`)."
- [x] 3.2 At `AGENTS.md:495`, lower the documented stack sizes
      from "Sensor Monitor task has 8KB stack, Network task has
      10KB stack" to the actual 6 KB / 8 KB values produced by
      the HWM-driven resize at `src/Network.cpp:993` and
      `src/task/SensorMonitor.cpp:33`.

## 4. Update `README.md` — Hardware table and constraints (#10, #11)

- [x] 4.1 At `README.md:28`, drop `(no PSRAM)` from the primary-
      board line and document ~320 KB internal + ~2 MB PSRAM.
- [x] 4.2 At `README.md:35-36`, keep the RAM row for the
      *internal* pool and add a separate row for the ~2 MB
      PSRAM. The CPU row stays as single-core ESP32-S2 @ 240 MHz.
- [x] 4.3 At `README.md:374`, replace "**No PSRAM**: ESP32-S2 has
      no PSRAM" with the corrected shape matching the
      `AGENTS.md:492` update.
- [x] 4.4 At `README.md:376`, lower "Stack size: Sensor Monitor
      task has 8KB stack, Network task has 10KB stack" to the
      actual 6 KB / 8 KB values.

## 5. Update `docs/CODE_REVIEW.md` — mark findings resolved

- [x] 5.1 In `docs/CODE_REVIEW.md:241-250` (finding #10), restyle
      the heading as resolved in the shape of the existing
      `### 1. ~~...~~ (resolved)` entries — strike the title and
      append " **(resolved)**"; add a **Resolution.** block
      pointing at the change (`fix-doc-spec-drift` /
      `archive/2026-MM-DD-fix-doc-spec-drift/`) and the touched
      files (`openspec/specs/system-architecture/spec.md`,
      `AGENTS.md`, `README.md`). State the new spec values (≥6 KB
      / ≥4 KB) and the in-tree values (8192 / 6144).
- [x] 5.2 In `docs/CODE_REVIEW.md:252-262` (finding #11), same
      treatment — strike the title and add a **Resolution.**
      block recording the corrected shape ("the board exposes
      ~2 MB PSRAM, verified on-device at `src/Network.cpp:555`;
      task stacks / FreeRTOS / WiFi / mbedTLS allocations
      remain internal-only per `AGENTS.md:460`").
- [x] 5.3 In `docs/CODE_REVIEW.md:264-272` (finding #12), same
      treatment — strike the title and add a **Resolution.**
      block recording that `AGENTS.md:265-279` and
      `AGENTS.md:504-508` now describe the shipped enum at
      `src/StatusLed.h:17-23` rather than the originally
      documented API.
- [x] 5.4 In `docs/CODE_REVIEW.md:274-279` (finding #13), same
      treatment — strike the title and add a **Resolution.**
      block recording that
      `openspec/specs/system-architecture/spec.md:91` now
      describes Network as holding a non-owning
      `WebServerManager*` to the file-scope instance, and the
      mode-flip scenario describes `setMode(...)` instead of
      `reset` / `make_unique`.

## 6. Verify and close out

- [x] 6.1 Re-read `AGENTS.md:265-279` and `AGENTS.md:504-508`
      against `src/StatusLed.h:17-23` and `src/StatusLed.cpp:36-63`;
      every name in the docs resolves to a real symbol, every
      colour description matches an actual `showColor(...)` call.
- [x] 6.2 Re-read `AGENTS.md:490-498` and `README.md:28-37,
      365-381`; the prose and the spec agree on the PSRAM shape,
      the stack sizes, and the internal-only constraint for OTA
      accounting.
- [x] 6.3 Re-read `docs/CODE_REVIEW.md:241-280` end-to-end; each
      of #10, #11, #12, #13 now reads in the shape of the
      surrounding **Resolved** entries and is internally
      consistent with the corrected `AGENTS.md`, `README.md`,
      and spec.
- [x] 6.4 Run `openspec validate --all --strict`; the change
      validates and the other specs / changes still pass without
      modification. (19/19 passed.)
- [x] 6.5 `pio test -e native` still passes (no code touched —
      verifies the `native` harness still compiles the sources
      that include `AGENTS.md` paths / spec-related headers).
      (469/469 succeeded in 24.753 s.)
- [x] 6.6 `pio run -e adafruit_qtpy_esp32s2` succeeds with the
      same flash / RAM shape as the baseline (no `src/` change,
      so this should be byte-equivalent). (RAM 25.0%, Flash
      73.2% — same as baseline.)
- [ ] 6.7 `/opsx:archive` to fold the spec delta into
      `openspec/specs/system-architecture/spec.md` and move the
      change to `openspec/changes/archive/`.
