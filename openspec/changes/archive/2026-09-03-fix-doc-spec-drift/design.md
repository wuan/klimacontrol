# Design: documentation / spec drift corrections (CODE_REVIEW #10, #11, #12, #13)

## Context

`docs/CODE_REVIEW.md` findings **#10**, **#11**, **#12**, **#13**
describe documentation/spec drift — every claim is testably wrong
against the shipped code, but OpenSpec validation is self-consistent
("the spec says what the spec says") so CI green-washes the gap. Each
finding has one correct fix (lower the spec to match measured code;
update the doc to match the shipped API); there is no architectural
decision to make. The design below records *which* form the
correction takes and why alternatives were rejected, so the next
reader of the spec/docs can verify the choice against the code in
60 seconds rather than reconstructing it from a diff.

The four findings, mapped to their canonical source:

| # | Finding | Source-of-truth it should cite |
|---|---------|--------------------------------|
| 10 | Network ≥ 14 KB, SensorMonitor ≥ 8 KB are stale | `src/Network.cpp:993` (8192), `src/task/SensorMonitor.cpp:33` (6144), with HWM justifications at `src/Network.cpp:976-989` and `src/task/SensorMonitor.cpp:22-29` |
| 11 | "No PSRAM" is wrong on this board | `src/Network.cpp:555` (`psram_size 2094735`); internal-only accounting at `src/Network.cpp:546-555` and `src/OTAUpdater.cpp` (gating via `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`) |
| 12 | `LedState::MEASURING/BLINK_SLOW/PULSE`, `setMeasuring()`, `setNormal()` don't exist | `src/StatusLed.h:17-23` (enum values), `src/StatusLed.cpp` (state→colour mapping) |
| 13 | Network *owns* WebServerManager is wrong | `src/main.cpp:137` (file-scope construction), `src/Network.h:145-168` (non-owning pointer), `src/Network.cpp:504-512` (mode-flip via `setMode(...)`, never destroy + reconstruct) |

## Goals / Non-Goals

**Goals:**

- Bring the OpenSpec capability specs (`system-architecture`),
  `AGENTS.md`, and `README.md` into one consistent narrative that
  matches the code at the four source-of-truth lines above.
- Preserve every existing requirement that still describes a real,
  honoured constraint. The drift is in stale *values* and
  stale *examples*, not in the underlying rules; nothing the spec
  currently requires is being relaxed.
- Leave a reader-visible trail of the measured-justification
  comments already in the code, so the spec and the codebase stay
  linked. The future ratio of "spec said 14 KB, code is 6 KB" is
  prevented by writing both the bound *and* the in-tree value into
  the same scenario.

**Non-Goals:**

- Refactoring `src/` code. Everything in `src/` is already correct
  for what it does; this change is *not* an excuse to "improve"
  the LED API or the ownership topology. If finding #12 motivates a
  richer LED API later, that's a separate change with its own
  spec additions.
- A general "audit every spec line against the code" sweep. The
  four findings in CODE_REVIEW.md priority order #8 are exactly
  the scope here; finding #14 (volatile ≠ atomic), #16
  (TOCTOU on `/api/status`), #17 (unbounded request bodies) etc.
  all warrant their own changes.
- A `RAML` / `arc42`-style structural rewrite of `AGENTS.md` or
  `README.md`. The corrections fit into the existing headings;
  adding new sections would dilute the change.
- Re-running `scripts/compress_web.py` or any data/web change. The
  web UI is unaffected.
- Touching the `native` test environment. `StatusLed.h:9-13` already
  compiles under `native` (it omits `<Arduino.h>` and
  `<Adafruit_NeoPixel.h>` via the `#ifdef ARDUINO` guard at line 9);
  the spec drifts are independent of which build target is being
  compiled.

## Decisions

### D1. Stack-size thresholds become absolute minima with a "current value" record

The current `### Requirement: FreeRTOS task structure` scenarios at
`openspec/specs/system-architecture/spec.md:43-51` carry single
`THEN` clauses stating "at least 14 KB" and "at least 8 KB". The
correction lowers the bound to "at least 6 KB" and "at least 4 KB"
respectively — values well below the in-tree 8192 / 6144 allocations
so the spec remains a *floor*, not a *ceiling*. Each `THEN` clause
also records the in-tree value and the HWM-justification comment
path, e.g.:

> **THEN** its stack is at least 6 KB (in-tree: 8192 B at
> `src/Network.cpp:993`; HWM-justified against measured peak 3544 B
> in the comment at `src/Network.cpp:976-989`).

Why an absolute minimum rather than "exactly N": the spec must
remain meaningful if someone later raises the in-tree stack to,
say, 12288 B. A "minimum 6 KB" floor protects against both
unintended reduction (the original drift) and unintended bloat
(the next reviewer's "for stability, double it" reflex).

Why not "match the in-tree value with no headroom": a buffer
removes the silent regression of "drop the stack one notch at a
time over six PRs", which is how the spec got here in the first
place (predates the HWM-driven resize, was apparently 10240 →
14336 → 8192 over time).

### D2. PSRAM availability is acknowledged; the internal-only constraint stays

`system-architecture/spec.md:8,13` is corrected from "PSRAM SHALL
NOT be assumed available" to:

> "the firmware runs on the Adafruit QT Py ESP32-S2 board, which
>  exposes ~2 MB of PSRAM (`psram_size ≈ 2094735` on the production
>  batch, recorded at `src/Network.cpp:555`). The task-stack and
>  FreeRTOS-allocator requirements in *Memory budget* are
>  *internal-SRAM-only* and survive this correction; PSRAM is
>  available for non-real-time allocations."

A new sentence in *Memory budget* (`spec.md:17-32`) makes the
internal-only constraint explicit:

> "OTA memory accounting uses
>  `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` — esp_get_free_heap_size
>  / `ESP.getFreeHeap()` include PSRAM (`CONFIG_SPIRAM_USE_MALLOC=y`)
>  and would pass unconditionally, but the allocations that fail
>  under pressure (task stacks, lwIP / WiFi structures, DMA
>  buffers, mbedTLS working set) are internal-only."

Why acknowledge PSRAM rather than hard-codify "no PSRAM" again:
hard-coding causes the spec to drift a second time the moment
someone swaps the board for the PSRAM-equipped variant or turns
the PSRAM Kconfig on. Acknowledging it as a fact of the platform
and constraining the load-bearing pieces (task stacks, OTA gate)
keeps the spec aligned with the Kconfig the firmware already ships.

### D3. AGENTS.md *Status LED Control* section is rewritten, not patched

Finding #12 affects five method references in `AGENTS.md:265-279`
that don't correspond to any symbol in the codebase. Patching each
one separately would leave a paragraph that references three
methods from the same enum that no compiler can resolve. The
section is rewritten as one block, mapping each documented method
to the actual one (or "no equivalent"):

```cpp
// Set LED states
statusLed.setState(LedState::ON);             // ON     - MQTT progress gradient
statusLed.setState(LedState::STARTUP);       // STARTUP - slow blue blink during boot
statusLed.setState(LedState::TRANSMIT_DATA); // TRANSMIT_DATA - brief white flash
statusLed.setState(LedState::ERROR);         // ERROR - solid red

// Convenience methods
statusLed.on();                // Same as setState(LedState::ON)
statusLed.off();               // Same as setState(LedState::OFF)
statusLed.setProgress(float);  // 0.0 = green, 1.0 = red in ON state
```

The `AGENTS.md:504-508` color summary ("Green: Normal / Yellow:
Active sensor measurement / Blue: AP mode / Red: Error") is
corrected in the same direction: `STARTUP` is the only state that
ever blinks blue, there is no AP-mode LED state, and there is no
yellow-while-measuring state in the shipped enum. The shipped
mapping is `OFF`/dark, `ON`/green→red gradient, `STARTUP`/slow
blue, `TRANSMIT_DATA`/brief white, `ERROR`/solid red.

### D4. WebServerManager ownership is recorded in `Ownership hierarchy`, not a new requirement

The current `### Requirement: Ownership hierarchy` at
`system-architecture/spec.md:89-102` already has the right shape
("`std::unique_ptr<T>` for owned resources, references for
non-owning"). Adding a separate "WebServerManager is constructed
once" requirement would duplicate `memory-management/spec.md:11-43`,
which already gets this right. The correction lives in the
existing `Ownership hierarchy` bullet ("Network owns the AsyncWebServer,
StatusLed, and WebServerManager instances") → ("Network owns the
AsyncWebServer and StatusLed instances; it holds a non-owning
`WebServerManager*` to the file-scope instance constructed once in
`main.cpp`"). The `WHEN`/`THEN` scenario for AP/STA transitions is
rewritten to describe `setMode(WebServerMode::CONFIG/OPERATIONAL)`
on the reused instance instead of `reset` + `make_unique`.

Why one bullet rather than a new requirement: the existing
requirement already names WebServerManager; saying "Network owns"
on one line and then "Network holds a non-owning pointer" on
another would force a future reviewer to read two requirements to
figure out which is true. A single corrected sentence preserves
the requirement's intent.

### D5. README.md and AGENTS.md memo paragraphs stay prose, not tables

The `README.md:365-381` *Memory Management / Constraints* sections
and `AGENTS.md:490-508` *Known Constraints* section are read like
a checklist. The corrections preserve that shape — bullet points
and table rows, not paragraph explanations. The "no PSRAM" bullet
becomes "320 KB internal + ~2 MB PSRAM; OTA gate and task stacks
internal-only". The "8 KB / 10 KB stack" line becomes "Sensor
Monitor task: 6 KB, Network task: 8 KB (periodically logged HWM)".

### D6. No new files; no renames

All four corrections are intra-section edits to existing files.
No new capability, no new test, no new doc. The OpenSpec archive
flow folds the spec deltas into `openspec/specs/system-architecture/
spec.md` and moves the change directory to
`openspec/changes/archive/`.

## Risks / Trade-offs

- **[Risk] The lowered stack-size floor silently legitimises a future regression.** Today the spec requires 14 KB / 8 KB and the code is at 8 KB / 6 KB — already below the spec. Lowering the floor to 6 KB / 4 KB pastes over the gap. → *Mitigation*: the spec scenarios record the in-tree value *and* the HWM-justification file path. A future PR that bumps the stack down past 6 KB will need to update that record, which is exactly what `/opsx-archive` flags. The `HWM` diagnostic lines (`SensorMonitor stack HWM:` / `Network task stack HWM:`) keep firing every 5 minutes, so a reviewer who actually reads the device output sees the new value go live.
- **[Risk] Acknowledging PSRAM invites "let's use it" comments in unrelated PRs.** → *Mitigation*: the spec names the specific constraint that keeps task stacks / OTA accounting off PSRAM (internal-only requirement in *Memory budget*) and the *Target hardware platform* requirement points readers at that constraint. Future PRs that want to use PSRAM bear the burden of explaining why their allocation is safe to make.
- **[Risk] AGENTS.md LED rewrite becomes stale the moment someone implements the missing states.** → *Mitigation*: the docs now match the shipped enum exactly; if a richer API lands, it ships as a separate spec delta under the `status-led` capability, not as a silent doc upgrade. The `status-led/spec.md` capability itself (lines 17, 29-35) is already aligned with the shipped enum, so any future LED feature rides through `status-led/spec.md`, which is the documented capability boundary.
- **[Risk] README's RAM row underspecifies what counts as "RAM".** Currently the row is "320 KB", which is true for internal SRAM but uninformative about the PSRAM-equipped board. → *Mitigation*: the row stays "~320 KB internal + ~2 MB PSRAM" — the short form preserves the constraint that OTA / FreeRTOS work against 320 KB; the "+PSRAM" suffix documents what's additionally available for non-real-time uses (none in this firmware today, but documented for the reader).
- **[Trade-off] No new tests / no new source.** A pure-documentation change doesn't naturally produce a regression test; verification reduces to "spec validates, docs read coherently, code still builds and tests pass". The change is small enough that this is the right trade — adding a documentation-shaped test would be cargo-culting.
- **[Trade-off] We do not collapse `memory-management/spec.md` *Long-lived singletons are constructed once* into `system-architecture/spec.md` *Ownership hierarchy*.** They duplicate the WebServerManager non-reconstruction statement. Collapsing would force one capability to take an editorial dependency on the other; the existing split (system-architecture = structural rules, memory-management = lifecycle / reallocation rules) is the documented split and survives.

## Migration Plan

No migration. The change is six prose edits to four files; none
of them affect NVS schema, OTA firmware shape, runtime behaviour,
or the public API. The OpenSpec archive step moves the change
directory from `openspec/changes/fix-doc-spec-drift/` to
`openspec/changes/archive/2026-MM-DD-fix-doc-spec-drift/` and
folds the spec delta into `openspec/specs/system-architecture/
spec.md`. The git diff between this commit and the previous
release commit is mechanical to review.

Rollback is `git revert`. There is no data migration, no flag
day, no NVS erase.

## Open Questions

None. The four findings each have one canonical fix; the design
choices (record the in-tree value alongside the bound; acknowledge
PSRAM but keep the internal-only constraint; collapse
WebServerManager ownership into the existing requirement) are
each the simplest form of the correction that prevents the spec
from drifting the same way a second time.
