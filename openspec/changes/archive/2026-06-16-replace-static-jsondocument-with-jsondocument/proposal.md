## Why

The firmware pins `bblanchon/ArduinoJson@^7.4.2`. In ArduinoJson 7, the
template `StaticJsonDocument<N>` is deprecated; the library's preferred
type is the unified `JsonDocument` constructed with the default
allocator. The 22 call sites under `src/routes/*.cpp` and
`src/WebServerManager.cpp` all use the old form, so every translation
unit emits a deprecation warning, the codebase drifts from the upstream
idioms, and future ArduinoJson 8 will not compile the old form at all.

In v7.4.2 `StaticJsonDocument<N>` is reduced to a deprecated thin
compatibility shim around `JsonDocument` with a phantom `capacity()`
(see `.pio/libdeps/.../ArduinoJson/src/ArduinoJson/compatibility.hpp:62-71`);
the v6 fixed-size in-object buffer is gone, so today's
`StaticJsonDocument<512> doc;` already hits the heap via the default
allocator on overflow. Migrating to plain `JsonDocument` removes the
deprecation noise without changing today's actual allocation behaviour.

## What Changes

- Replace every `StaticJsonDocument<N> doc;` in `src/routes/*.cpp` and
  in `src/WebServerManager.cpp` with the v7 idiom:
  `JsonDocument doc;`. The document object itself is stack-allocated on
  the handler's stack frame, exactly like the old form; variable-length
  content is allocated via `JsonDocument`'s default allocator and freed
  when the document is destroyed at handler return.
- Drop the byte-size template argument. The 128 / 512 / 1024 / 2048
  sizes are removed because they no longer correspond to a backing
  buffer; capacity is now governed entirely by the default allocator
  (`heap_caps_malloc` / `free` on ESP32, `malloc` / `free` on native).
- Continue to forbid `std::make_unique<JsonDocument>()` and
  `new JsonDocument` in route handlers — the document object itself
  must live on the stack frame so its destructor runs deterministically
  when the handler returns. This is the discipline that keeps per-request
  allocations bounded to the document's overflow data, not a
  heap-allocated document wrapper.
- Loosen the existing "no heap allocation in a request handler" claim
  in `openspec/specs/http-api/spec.md` and
  `openspec/specs/memory-management/spec.md`. The new contract is:
  the `JsonDocument` object MUST be stack-allocated in the handler;
  any data the document holds is allocated via the default allocator
  and freed at handler return; `std::make_unique<JsonDocument>()`
  and `new JsonDocument` remain forbidden.
- Update the `JSON buffer` note in `AGENTS.md` (currently says
  `StaticJsonDocument<512>`) to reflect the new idiom.
- The existing non-template `JsonDocument` usages in
  `src/OTAUpdater.cpp:144,150` are out of scope — they are already on
  the v7 idiom.

No JSON wire format change, no API change, no new dependency, no
schema change. The two affected spec requirements change to reflect
the new (real) allocation discipline; the per-handler `JsonDocument`
remains on the stack frame and is destroyed at handler return.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `http-api`: the `Request handler allocation discipline` requirement
  and its two scenarios (GET handler builds a status response; POST
  handler parses a JSON body) currently mandate a stack-allocated
  `StaticJsonDocument` with no per-handler heap allocation. Rewrite
  to mandate a stack-allocated `JsonDocument` (the document object on
  the stack frame; data allocated via the default allocator and freed
  at handler return) and continue to forbid
  `std::make_unique<JsonDocument>()` / `new JsonDocument` /
  `DynamicJsonDocument`.
- `memory-management`: the `HTTP route handlers do not allocate from
  the heap` requirement and its two scenarios (status handler;
  WiFi config POST) are renamed and rewritten as
  `HTTP route handlers place the JsonDocument on the stack` —
  the document object is stack-allocated, any data it holds is
  allocated via the default allocator and freed at handler return,
  and `std::make_unique<JsonDocument>()` / `new JsonDocument` /
  `DynamicJsonDocument` remain forbidden. The renamed requirement
  is in scope for this change.

## Impact

- **Source files (refactor)**: `src/routes/StatusRoutes.cpp`,
  `src/routes/SyslogRoutes.cpp`, `src/routes/ControlRoutes.cpp`,
  `src/routes/OTARoutes.cpp`, `src/routes/MqttRoutes.cpp`,
  `src/routes/I2CRoutes.cpp`, `src/routes/SettingsRoutes.cpp`,
  `src/routes/SensorRoutes.cpp`, `src/WebServerManager.cpp`. 22
  call sites.
- **Spec files (text update)**: `openspec/specs/http-api/spec.md`,
  `openspec/specs/memory-management/spec.md` (rename + rewrite of
  the relevant requirement, plus its two scenarios in each spec).
- **Docs (informational)**: `AGENTS.md` (one line under "Known
  Constraints").
- **No dependency changes**: `bblanchon/ArduinoJson@^7.4.2` already
  installed; v7 is the source of truth for the new idiom.
- **Runtime impact**: none observed — the existing v7.4.2
  `StaticJsonDocument<N>` was already heap-allocating on overflow
  via the default allocator, so today's per-request behaviour is
  preserved. The on-stack frame size for the document object
  shrinks (the `JsonDocument` object is a fixed small size; the
  per-N inline buffer is gone), freeing a few hundred bytes of
  stack per handler.
- **Tests**: native test build is unaffected (the route handlers
  are behind `#ifdef ARDUINO`); a compile run on the ESP32 target
  plus a `pio test -e native` run are the only verification steps.
- **Out of scope**: the existing non-template `JsonDocument` uses
  in `src/OTAUpdater.cpp` (already v7-idiomatic), heap
  `JsonDocument` mentions in docs, and `DynamicJsonDocument`
  examples under `docs/` and `examples/`.
