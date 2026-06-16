## Context

The firmware depends on `bblanchon/ArduinoJson@^7.4.2`
(`platformio.ini:38`). ArduinoJson 7 unifies the document model:
`StaticJsonDocument<N>` and `DynamicJsonDocument` are both
deprecated/removed in favour of a single non-templated
`JsonDocument` that uses the library's default allocator (on ESP32,
`heap_caps_malloc` / `free`).

The codebase currently uses the v6 idiom in every HTTP route
handler — `StaticJsonDocument<512> doc;` (and the rarer
`<1024>`, `<2048>`, `<128>` sizes). 22 call sites across
`src/routes/*.cpp` and `src/WebServerManager.cpp`. These
handlers are all guarded by `#ifdef ARDUINO` so they only
build on the ESP32 target; the native test environment does
not see them.

Investigation of the v7.4.2 source surfaced a critical detail:
in v7 `StaticJsonDocument<N>` is a deprecated shim that inherits
`JsonDocument`'s constructors (default = heap-backed) and reports
a phantom `capacity()` returning `N` (see
`.pio/libdeps/adafruit_qtpy_esp32s2/ArduinoJson/src/ArduinoJson/compatibility.hpp:62-71`).
The v6 fixed-size in-object buffer is gone, so today's
`StaticJsonDocument<512> doc;` in route handlers is already
heap-allocating on overflow via the default allocator. The
existing "no heap allocation in a request handler" claim in
`http-api` and `memory-management` was always partly aspirational
under v7 — the v6 contract is no longer enforced by the type.

The proposal-level decision is therefore to: (a) drop the old N
buffer and use plain `JsonDocument doc;`, (b) keep the discipline
that the document *object* itself is stack-allocated on the
handler's stack frame, and (c) update the two affected specs to
reflect the real allocation behaviour.

## Goals / Non-Goals

**Goals:**

- Replace every `StaticJsonDocument<N>` in route handlers and
  `WebServerManager.cpp` with the ArduinoJson 7 idiom `JsonDocument
  doc;`.
- Keep the discipline that the document object is on the handler's
  stack frame (no `std::make_unique<JsonDocument>()`, no
  `new JsonDocument`); the document's destructor runs
  deterministically at handler return and frees any data the
  default allocator gave it.
- Remove the deprecation noise from the build so the codebase
  stays aligned with the upstream library.
- Update the two affected specs so the type name in the spec
  matches the type name in the code, and the requirement
  language reflects the real allocation model.

**Non-Goals:**

- Changing the JSON wire format of any endpoint.
- Bumping the ArduinoJson dependency.
- Implementing a custom stack allocator (option 1 from the
  pause) — rejected because the codebase already accepted
  heap-backed `JsonDocument` semantics under the v7 shim, and
  the user opted for the smaller change.
- Reworking the existing non-template `JsonDocument` uses in
  `src/OTAUpdater.cpp` (already v7-idiomatic).
- Touching `DynamicJsonDocument` examples in `docs/` and
  `examples/OTA_INTEGRATION_EXAMPLE.cpp` (docs only, not
  firmware).
- Adding a thread-local pool or any other restructuring
  beyond the type swap.

## Decisions

### D1. Use plain `JsonDocument doc;` (no buffer, no custom allocator)

Every replacement follows the same shape:

```cpp
JsonDocument doc;
// ... populate doc ...
serializeJson(doc, output);
```

- The `JsonDocument` object itself is on the stack frame
  (sizeof(JsonDocument) ≈ 32–64 bytes on the toolchain;
  contains a `ResourceManager` and a root `VariantData`).
- Variable-length data the document holds (string contents,
  nested object/array payloads) is allocated via the
  ArduinoJson default allocator — `heap_caps_malloc` /
  `free` on ESP32, `malloc` / `free` on the host — and freed
  when the document is destroyed at handler return.
- The handler is still bounded: one document per request,
  destroyed at handler exit, no `make_unique` factory, no
  `new JsonDocument`. This is the discipline the new spec
  asserts.

**Alternatives considered**

- *Custom `StackAllocator` that hands out pointers into a
  `std::array<char, N>`.* Rejected by the user (option 2). It
  would have preserved the no-heap-on-overflow invariant
  but at the cost of ~30 lines of plumbing per TU.
- *Reuse a `thread_local JsonDocument` across requests.*
  Out of scope; explicitly excluded by the archived
  `2026-06-15-reduce-heap-fragmentation` change's design
  (see `openspec/changes/archive/2026-06-15-reduce-heap-fragmentation/design.md:99-126`).
  Per-handler stack instances remain the current and desired
  pattern.
- *Keep `StaticJsonDocument<N>` and silence the
  deprecation.* Rejected — the v7.4.2 shim is
  already heap-allocating on overflow via the default
  allocator, so keeping it buys nothing and locks the
  codebase into a deprecated form that ArduinoJson 8 is
  expected to remove entirely.

### D2. Drop the byte-size template argument

Map each existing template argument to a plain `JsonDocument`:

| Old | New |
|---|---|
| `StaticJsonDocument<128> responseDoc;` | `JsonDocument responseDoc;` |
| `StaticJsonDocument<512> doc;` | `JsonDocument doc;` |
| `StaticJsonDocument<1024> doc;` | `JsonDocument doc;` |
| `StaticJsonDocument<2048> doc;` | `JsonDocument doc;` |

The on-stack byte budget per request shrinks (the per-N inline
buffer is gone), freeing a few hundred bytes of stack per
handler. Capacity is now governed entirely by the default
allocator and is effectively unbounded up to available heap.

**Rationale.** The old 512/1024/2048 sizes were ceilings
encoded in the type. With v7's `JsonDocument` the only way to
get a fixed budget is a custom allocator (D1's rejected
option), which we are not doing. The pragmatic call is to
accept the library's default behaviour and document it in
the spec.

### D3. Spec text follows the code, not the other way around

`openspec/specs/http-api/spec.md:79-91` and
`openspec/specs/memory-management/spec.md:44-65` both name
`StaticJsonDocument` in their requirement language and
scenarios, and both assert "no heap allocation" for the
document object. The proposal-level Capabilities section
already calls these out as the two specs to update; the
spec-delta files under `openspec/changes/.../specs/` will:

- Rename the `memory-management` requirement from
  "HTTP route handlers do not allocate from the heap" to
  "HTTP route handlers place the JsonDocument on the stack"
  and rewrite it to: the document object MUST be
  stack-allocated on the handler's frame; any data the
  document holds is allocated via the default allocator
  and freed when the document is destroyed; `make_unique`,
  `new JsonDocument`, and `DynamicJsonDocument` remain
  forbidden.
- Update the `http-api` "Request handler allocation
  discipline" requirement language to match.
- Keep the scenario WHEN clauses intact; update the THEN
  clauses to assert the new (real) behaviour.

**Rationale.** Specs are the contract. The real discipline
in v7 is "document on stack, data via default allocator,
freed at handler return" — not "absolutely no heap
allocation". The old text overstated what the type
enforced; the new text matches the runtime.

### D4. Update the AGENTS.md note

`AGENTS.md:464` says "`StaticJsonDocument<512>`" under
"Known Constraints". Reword to "`JsonDocument` on the
handler's stack frame; data via the default allocator,
freed at handler return" so the at-a-glance reference
matches the code.

## Risks / Trade-offs

- **Risk:** A handler that previously fit inside the
  `StaticJsonDocument<512>` inline budget will now allocate
  that data on the heap via the default allocator.
  **→** No regression: the v7.4.2 `StaticJsonDocument<N>`
  shim was *already* allocating on overflow via the default
  allocator, so the wire format and the allocation pattern
  for any handler that overflowed are unchanged. For
  handlers that did not overflow, the new behaviour is
  one extra small `malloc` per request — bounded by the
  document's actual size and freed at handler return. No
  mitigation needed beyond the existing test coverage in
  `test/`.
- **Risk:** Confusion between `JsonDocument` (heap-backed
  in v7) and the older `DynamicJsonDocument` (also
  deprecated in v7). **→** Mitigation: the design + spec
  delta explicitly call out the v7 allocation model;
  tasks reference the D1 snippet so each call site
  follows the same shape; `rg "DynamicJsonDocument" src/`
  is part of the audit (already returns no matches).
- **Risk:** Forgetting a site — leaving one
  `StaticJsonDocument<>` in the tree. **→** Mitigation:
  the tasks list explicitly enumerates every file and
  the apply step greps for residual `StaticJsonDocument`
  after the rename; the build itself won't fail (the
  deprecated form still compiles under v7) but a single
  `rg "StaticJsonDocument" src/` is the audit.
- **Risk:** Per-request `malloc` for handlers whose
  payload fits in the old inline budget. **→** Acceptable
  trade-off; the user chose option 2 explicitly. The
  payloads in this codebase are small (status fields,
  config values) and the `largest_free_block` metric in
  `/api/status` already exposes any fragmentation
  regression.

## Migration Plan

This is a refactor; no runtime deploy step is needed
beyond rebuilding and re-flashing the firmware.

1. Update spec deltas in this change (`specs/http-api/spec.md`,
   `specs/memory-management/spec.md`).
2. Apply the source rename in `src/routes/*.cpp` and
   `src/WebServerManager.cpp` per D1/D2.
3. Update `AGENTS.md` per D4.
4. Build for ESP32: `pio run -e adafruit_qtpy_esp32s2`.
   Expect zero `StaticJsonDocument` deprecation warnings
   on route-handler translation units.
5. Run native tests: `pio test -e native`. Unaffected by
   the change (routes are behind `#ifdef ARDUINO`), but
   guards against accidental include-graph breakage.
6. Archive the change (`/opsx:archive`).

**Rollback:** revert the source/spec/AGENTS commits. The
code is purely a type rename; the wire format is
unchanged; the allocation pattern matches what the v7.4.2
`StaticJsonDocument<N>` shim was already doing. No data
migration; NVS schema is untouched.

## Open Questions

None. The migration is a name-for-name swap, the v7
idiom is documented by the upstream library, and the
existing two specs (http-api, memory-management) are
updated to reflect the real allocation behaviour.
