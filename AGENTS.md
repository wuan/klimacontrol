# AGENTS.md

Guidance for AI coding agents working in this repository. Keep this file to
things the code cannot teach: gotchas, rationale, and conventions that differ
from defaults. Layouts, commands, endpoints and APIs are derivable from the
repo — read the source instead of duplicating it here.

Klima-Control is ESP32-S2 (Adafruit QT Py) firmware built with PlatformIO
(`pio run -e adafruit_qtpy_esp32s2`, tests via `pio test -e native`).

## Spec-Driven Development (OpenSpec)

Specs under `openspec/` are the source of truth for *what* each capability
must do; the code is the implementation.

- **The `openspec` CLI and all skills must run from the repo root.** Run from
  anywhere else and the CLI silently reports "No items found to validate."
- **When a change needs a spec update:** anything that adds, removes, or alters
  observable behavior of a capability — propose, update the relevant
  `spec.md`, implement, then archive. Pure refactors, bug fixes that restore
  already-specified behavior, and build/tooling tweaks don't need a change.
- **Workflow:** `/opsx:explore` (optional) → `/opsx:propose` → `/opsx:apply` →
  `/opsx:archive`. Validate with `openspec validate --all --strict`
  (or `scripts/validate-openspec.sh`); CI runs the same on every push/PR
  touching `openspec/**`.
- After upgrading the CLI or adding an AI tool, run `openspec update` from the
  repo root to refresh the generated tool-integration files under `.claude/`.

## Conventions

- **Ownership:** no raw pointers for resource ownership. Owned resources are
  `std::unique_ptr<T>`, transferred with `std::move()`; non-owning access is a
  reference (`T&`), never a raw pointer. Never call `new` without immediately
  wrapping it.
- **Logging:** use `ESP_LOGx` with a per-file `TAG`, not `Serial.printf()`.
- **Platform guards:** wrap ESP32-specific code (WiFi, FreeRTOS, `Serial`) in
  `#ifdef ARDUINO`. The `native` environment exists for tests only: it builds
  sensor utilities, temperature control, and core data structures, not
  network/webserver or hardware code.
- **Config:** access via the `Config::ConfigManager` singleton; call
  `config.begin()` in `setup()` before any other use.
- **JSON in route handlers:** `JsonDocument` lives on the handler's stack
  frame; variable-length data goes through ArduinoJson's default allocator and
  is freed at handler return. The document object itself MUST NOT be
  heap-allocated (`make_unique<JsonDocument>` / `new JsonDocument` are
  forbidden in route handlers).

## Sensors

- Multiple sensors are **not averaged**: averaging a faulty reading with a
  healthy one would still produce a contaminated value. Per-driver range
  validation rejects implausible readings before they reach the controller
  (see `openspec/specs/sensor-management/spec.md`).
- The control loop lives in `Control::TemperatureController`, fed by the
  sensor monitor task via `SensorController::getProcessValue()`; it holds no
  sensor reference and is tested natively.

## Status LED

The shipped `LedState` enum (`src/StatusLed.h`) is exactly
`OFF, ON, STARTUP, TRANSMIT_DATA, ERROR`:

- `OFF` — dark
- `ON` — MQTT publish progress gradient (green freshly published → red just
  before the next publish). *Not* a steady "green = normal" indicator.
- `STARTUP` — slow dark-blue blink during boot and WiFi association
- `TRANSMIT_DATA` — brief near-white flash during an MQTT publish
- `ERROR` — solid red for fatal init errors (e.g. mutex allocation failure)

There is no `MEASURING`, `BLINK_SLOW`, `PULSE`, AP-mode, or yellow "measuring"
state, and no `setMeasuring()` / `setNormal()` shortcuts. Don't invent them.

## Memory (ESP32-S2)

- **Measure headroom against internal SRAM only.** `esp_get_free_heap_size()`
  / `ESP.getFreeHeap()` include the ~2 MB PSRAM (`CONFIG_SPIRAM_USE_MALLOC=y`,
  `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`), but the allocations that fail
  under pressure — task stacks, lwIP/WiFi structures, DMA buffers, the
  mbedTLS working set — are internal-only. The OTA gate and the network task's
  low-heap restart guard both use `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`.
- Task stack sizes are HWM-driven; the periodic "stack HWM" log lines carry
  the live measurement. Check them before changing a stack size.
- Single core: no cross-core assumptions, all tasks share one core.

## OTA updates

- **Both OTA tasks are created once by `OTAUpdater::begin()` in `setup()` and
  park on a task notification forever. They are never deleted.** Their stacks
  are static BSS buffers (a FreeRTOS stack needs contiguous *internal* SRAM,
  which is unreliable to allocate once WiFi/mbedTLS have fragmented the heap),
  and `vTaskDelete()` only queues a task for reclamation by the idle task — so
  recreating a task on the same `StaticTask_t` before idle has run corrupts
  the scheduler's lists. Check and update are mutually exclusive via a single
  atomic `Activity` claimed with one compare-exchange.
- **The release asset must be named exactly `firmware.bin`**
  (`OTA_FIRMWARE_ASSET`). The device deliberately does *not* take the first
  `.bin`: a filesystem image, bootloader blob, or another board's build would
  pass `esp_ota_set_boot_partition()`'s verification (it is a valid image, just
  not one this board can run) and boot-loop the device into USB recovery.
- **Tags must be `vMAJOR.MINOR.PATCH`.** Versions are compared by semver
  ordering (`src/support/VersionCompare.h`); unparseable tags are ignored, and
  only a *strictly newer* release is offered, so a `git describe` dev build is
  never handed a downgrade.
- **No client-supplied URL:** `POST /api/ota/update` carries no URL; the device
  flashes only what its own check of the compiled-in owner/repo found.
  `performUpdate()` is private for the same reason. The
  `https://github.com/` allowlist covers only the first hop; the CDN redirect
  is checked separately and must stay on `https`.
- **Rollback confirm timing:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, so a
  new image boots `ESP_OTA_IMG_PENDING_VERIFY` and reverts on the next reset
  unless confirmed. `OTAUpdater::confirmRunningImage()` runs at the END of
  `setup()` — late enough that a crash during init still rolls back, early
  enough that no automatic restart path (low-heap guard, WiFi force-restart,
  watchdog, power cycle) can revert a working update. Don't move it.

## Naming

Device IDs are `klima-AABBCC` (last 3 bytes of the MAC); the mDNS hostname is
the lowercase form, `klima-aabbcc.local` (see `Net::MdnsAdvertiser::hostname()`).
