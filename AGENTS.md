# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Klima-Control** is an ESP32-based temperature controller with web interface for monitoring and controlling temperature and humidity. It runs on the Adafruit QT Py ESP32-S2 and uses a variety of sensors connected via the Stemma I2c port for precise environmental monitoring.

**Key characteristics:**
- Single-core architecture optimized for ESP32-S2
- Real-time temperature and humidity monitoring
- Web-based configuration and control interface
- Persistent storage via ESP32 Preferences (NVS)
- mDNS support for easy discovery (e.g., `klima-zaabbcc.local`)
- AP mode for initial WiFi setup with captive portal
- Visual status feedback via built-in NeoPixel LED

## Build Commands

### Build for ESP32 (primary target)
```bash
pio run -e adafruit_qtpy_esp32s2
```

### Build for native environment (tests only)
```bash
pio run -e native
```

### Run tests
```bash
# Run all tests
pio test -e native

# Run specific test suite
pio test -e native -f test_sensor
pio test -e native -f test_temperature
pio test -e native -f test_control

# Verbose output
pio test -e native -v
```

### Upload to device
```bash
pio run -e adafruit_qtpy_esp32s2 -t upload
```

### Monitor serial output
```bash
pio device monitor
```

## Spec-Driven Development (OpenSpec)

This project uses [OpenSpec](https://github.com/Fission-AI/OpenSpec) to keep a written specification alongside the firmware. The specs are the source of truth for *what* each capability must do; the code is the implementation.

**Layout** (all under `openspec/` at the repo root):

| Path | Purpose |
|------|---------|
| `openspec/config.yaml` | Schema (`spec-driven`) + project `context` shown to AI when generating artifacts |
| `openspec/specs/<capability>/spec.md` | One living spec per capability (`### Requirement` + `#### Scenario` WHEN/THEN syntax) |
| `openspec/changes/<name>/` | An in-flight change: `proposal.md`, `design.md`, `specs/` deltas, `tasks.md` |
| `openspec/changes/archive/` | Completed, archived changes |

There are 11 capability specs: `system-architecture`, `sensor-management`, `http-api`, `networking`, `network-wifi-resilience`, `ota-updates`, `mqtt-integration`, `web-interface`, `configuration`, `status-led`, `temperature-control`.

**The CLI and all skills must run from the repo root** (where `openspec/` lives). Run from anywhere else and the CLI silently reports "No items found to validate."

**When to use it:** any change that adds, removes, or alters observable behavior of a capability should ride alongside a spec update — propose the change, update the relevant `spec.md`, implement, then archive. Pure refactors, bug fixes that restore already-specified behavior, and build/tooling tweaks don't need a change.

**Workflow** (Claude Code skills / `/opsx:*` commands drive each step):

1. **Explore** (optional) — `/opsx:explore` to think through the problem before committing to a change.
2. **Propose** — `/opsx:propose` scaffolds `openspec/changes/<name>/` and generates proposal, design, spec deltas, and tasks.
3. **Apply** — `/opsx:apply` implements the tasks, checking them off as it goes.
4. **Archive** — `/opsx:archive` folds the spec deltas into the main specs under `openspec/specs/` and moves the change to `archive/`.

**Validate** (run from repo root):
```bash
openspec validate --all --strict     # or: scripts/validate-openspec.sh
openspec list                          # active changes
openspec list --specs                  # capability specs
```

CI runs the same validation on every push/PR that touches `openspec/**` (`.github/workflows/openspec.yml`). After upgrading the CLI or adding an AI tool, run `openspec update` from the repo root to refresh the generated tool-integration files under `.claude/`.

## Architecture

### Single-Core Task Model

The ESP32-S2's single core is utilized with FreeRTOS tasks for responsive operation:

- **Network Task**: Runs `Network::task()` - handles WiFi, NTP, mDNS, webserver, and API
- **Sensor Monitor Task**: Runs `SensorMonitor::task()` - reads sensors and updates temperature control

**Task communication**: Uses direct method calls and shared data with proper synchronization.

### Critical Task Rules

1. **Network Task** handles all web requests and API calls
2. **Sensor Monitor Task** reads sensors and updates control state
3. **Status LED** provides visual feedback (green=normal, yellow=measuring, blue=AP mode, red=error)
4. All sensor data access is thread-safe through the SensorController

### Component Relationships

**Ownership hierarchy** (via `std::unique_ptr` and references):
```
main.cpp (setup)
├─> SensorController (owns: sensors, data logger)
│   ├─> Config& (reference, persistence to NVS)
│   └─> Network& (reference, for status LED control)
├─> Network (owns: webServer, statusLed, etc.)
│   ├─> Config& (reference)
│   ├─> SensorController& (reference)
│   └─> WebServerManager (owned by Network)
│       ├─> Config& (reference)
│       ├─> Network& (reference)
│       └─> SensorController& (reference)
```

### Temperature Control System

**Sensor architecture**:
- `SensorController` manages multiple sensor instances
- `SHT4x` sensor implementation for temperature/humidity
- Per-driver range validation rejects implausible readings before they reach the controller (see `openspec/specs/sensor-management/spec.md` → "Per-driver range validation"). Multiple sensors are not averaged — averaging a faulty reading with a healthy one would still produce a contaminated value
- Data logging for historical analysis

**Control flow**:
1. Sensors are read at configured intervals (default: 1 second)
2. `SensorController::readSensors()` collects and validates data
3. Temperature control algorithm calculates output (PID control)
4. Status LED shows yellow during measurement, green when idle
5. Web interface displays real-time data and control status
6. Temperature control algorithm runs every second
7. All measurements and control states are logged for analysis

**Temperature Control Flow**:
1. Sensors read at configured interval (default: 1 second)
2. `SensorController::readSensors()` collects and validates data
3. Valid readings are stored
4. Temperature control calculates output using PID algorithm
5. Status LED provides visual feedback during each phase
6. Web interface updates in real-time via API

### Sensor Architecture

**Sensor Management**: The `SensorController` manages multiple sensor instances with per-driver range validation and `SensorStatus` tracking (`Online`, `InitFailed`, `ReadFailing`).

**SHT4x Integration**: Primary temperature/humidity sensor with:
- Range: -40°C to +125°C, 0-100% RH
- Precision: ±0.1°C, ±1.5% RH
- I2C interface with configurable address

**Data Flow**:
1. Individual sensors read at configured intervals
2. Valid readings are averaged for accuracy
3. Combined data stored with timestamp
4. Historical data logged for trend analysis
5. Real-time data served via API endpoints

### Configuration Persistence

Uses ESP32 Preferences API (NVS) with namespace "ledctrl":

- **WiFiConfig**: SSID, password, configured flag, connection failure counter
- **DeviceConfig**: device_name, device_id, sensor_i2c_address
- **TemperatureConfig**: target_temperature, control_enabled

Access via `Config::ConfigManager` singleton. Always call `config.begin()` in setup before use.

### Network Modes

**AP Mode** (Access Point):
- Starts on first boot or after 3 connection failures
- SSID format: `klima-AABBCC` (from MAC address last 3 bytes)
- IP: 192.168.4.1
- Captive portal redirects all DNS queries to device
- Restarts automatically after WiFi configuration received

**STA Mode** (Station/Client):
- Connects to configured WiFi network
- Advertises via mDNS as `klima-aabbcc.local`
- Updates NTP time every 300 seconds
- Auto-reconnects if connection lost
- Increments failure counter on failed connection (triggers AP mode after 3 failures)

### WebServerManager Structure

**Embedded HTML**: The entire web interface is embedded as C++ string literals in `WebServerManager.cpp` using raw string literals. This eliminates filesystem dependencies.

**Key endpoints**:
- `GET /` - Main temperature control interface
- `GET /config` - WiFi configuration page (AP mode)
- `GET /api/status` - Device status, temperature, humidity, and control status
- `POST /api/settings/device` - Update device settings (name, etc.)
- `POST /api/settings/factory-reset` - Factory reset (erases NVS)
- `GET /api/sensors` - Detailed sensor information

**Temperature control endpoints**:
- `GET /api/temperature` - Current temperature and target
- `POST /api/temperature/target` - Set target temperature
- `POST /api/control/enable` - Enable/disable temperature control

## Important Patterns & Conventions

### Memory Management

**The project uses zero raw pointers for resource ownership.** All dynamic memory is managed via `std::unique_ptr` with C++ move semantics for ownership transfer.

**Ownership rules**:
- Use `std::unique_ptr<T>` for owned resources
- Transfer ownership with `std::move()`: `owner = std::move(resource)`
- Non-owning access uses references: `T&` (never raw pointers)
- Factory functions return rvalue references: `std::unique_ptr<T>&&`

**Examples**:
```cpp
// Creating owned resource
auto base = std::unique_ptr<Strip::Base>(new Strip::Base(pin, num_pixels));

// Transferring ownership
showController.setStrip(std::move(base));  // base is now nullptr

// Factory returns unique_ptr via rvalue reference
std::unique_ptr<Show::Show> newShow = factory.createShow(name, params);
currentShow = std::move(newShow);

// Non-owning access via reference
WebServerManager(Config& config, Network& network, ShowController& controller);
```

**When adding new code**:
- NEVER use `new` without immediately wrapping in `std::unique_ptr`
- NEVER store raw pointers to owned resources
- Use references (`&`) for non-owning access, never raw pointers
- Pass `std::unique_ptr` by rvalue reference (`&&`) or via `std::move()`

### Sensor Integration Pattern

Sensors are managed through the `SensorController` with a clean interface:

```cpp
// Adding a sensor
auto sht4x = std::make_unique<Sensor::SHT4x>(sensor_address);
sensorController.addSensor(std::move(sht4x));

// Reading sensors (each sensor reports its own measurements; per-driver range
// validation rejects implausible values before they reach the controller)
Sensor::SensorData data = sensorController.readSensors();
if (data.valid) {
    float temperature = data.temperature;
    float humidity = data.humidity;
}

// Temperature control
sensorController.setTargetTemperature(22.0f);
sensorController.setControlEnabled(true);
float controlOutput = sensorController.updateControl();
```

### Status LED Control

The status LED provides visual feedback using the `StatusLed` class.
The shipped `LedState` enum at `src/StatusLed.h:17-23` is
`OFF, ON, STARTUP, TRANSMIT_DATA, ERROR`; the convenience methods
that exist on the class are `on()`, `off()`, `toggle()`,
`setProgress(float)`, `getProgress()`, `getState()`, and
`setState(LedState)`. There is no `LedState::MEASURING`,
`LedState::BLINK_SLOW`, or `LedState::PULSE` in the codebase, and
there are no `setMeasuring()` / `setNormal()` shortcuts.

```cpp
// Set LED states
statusLed.setState(LedState::OFF);            // Dark
statusLed.setState(LedState::ON);             // MQTT progress gradient (green→red)
statusLed.setState(LedState::STARTUP);        // Slow blue blink during boot / WiFi association
statusLed.setState(LedState::TRANSMIT_DATA);  // Brief white flash during MQTT publish
statusLed.setState(LedState::ERROR);          // Solid red — fatal init error

// Convenience methods
statusLed.on();                // Same as setState(LedState::ON)
statusLed.off();               // Same as setState(LedState::OFF)
statusLed.toggle();            // Flips between ON and OFF
statusLed.setProgress(float);  // 0.0 = green, 1.0 = red in ON state
statusLed.update();            // Drives the animation; call from the network task each iteration
```

### Serial Logging
Use `Serial.printf()` for formatted debug output. Key points to log:
- Sensor readings and control updates
- Network state changes (AP/STA mode)
- Temperature control calculations
- System status and errors

## File Locations

**Core components**: `src/` (main.cpp, Config, Network, SensorController, WebServerManager)
**Sensors**: `src/sensor/` (Sensor.h, SHT4x.h/cpp for temperature/humidity sensing)
**Task system**: `src/task/` (SensorMonitor.cpp/h for sensor reading task)
**Utilities**: `src/support/` (color utilities, data structures)
**Tests**: `test/test_*/` (each subdirectory is independent test suite)
**Documentation**: `docs/` (technical documentation)
**Specifications**: `openspec/` (spec-driven capability specs + change workflow — see [Spec-Driven Development](#spec-driven-development-openspec))

## Platform-Specific Code

Use `#ifdef ARDUINO` to guard ESP32-specific code:
```cpp
#ifdef ARDUINO
    WiFi.begin(ssid, password);
    vTaskDelay(500 / portTICK_PERIOD_MS);
#endif
```

Native environment is for testing only - it builds sensor utilities, temperature control logic, and core data structures but not network/webserver or hardware-specific code.

## Common Modifications

### Adding a new sensor type
1. Create `src/sensor/MySensor.h` and `src/sensor/MySensor.cpp` inheriting from `Sensor::Sensor`
2. Implement `read()`, `isConnected()`, and `getName()` methods
3. Add sensor initialization in `SensorController::begin()`
4. Include header in `main.cpp`

### Adding new temperature control features
1. Add field to `Config.h` (DeviceConfig or TemperatureConfig)
2. Add load/save logic in `Config.cpp`
3. Add API endpoint in `WebServerManager.cpp`
4. Add UI controls in settings page HTML
5. Implement control logic in `SensorController::updateControl()`

### Modifying status LED behavior
1. Add new state to `LedState` enum in `StatusLed.h`
2. Add case in `StatusLed::setState()` to handle the new state
3. Add case in `StatusLed::update()` for display logic
4. Add convenience method if needed (e.g., `setCustomState()`)

## OTA Firmware Updates

The project includes Over-The-Air (OTA) firmware update capability using GitHub releases.

### Architecture

**Components**:
- `src/OTAUpdater.h` / `src/OTAUpdater.cpp` - Core OTA functionality
- `src/OTAConfig.h` - Configuration (GitHub repo, version, safety settings)
- `/api/ota/*` endpoints in WebServerManager - Web API
- Settings page - User interface for updates

**Update Flow**:
1. User clicks "Check for Updates" in Settings; the browser `POST`s `/api/ota/check` and polls `GET /api/ota/check`
2. The parked `ota_check` task runs `OTAUpdater::checkForUpdate()`, which queries the GitHub REST API for the latest release and locates the `firmware.bin` asset by exact name
3. If the release is *strictly newer* than `FIRMWARE_VERSION` (semver ordering, see `src/support/VersionCompare.h`), the UI shows version and size
4. User clicks "Install Update"; the browser `POST`s `/api/ota/update` (no URL is sent — the device installs only what its own check found) and polls `GET /api/ota/update` for progress
5. The parked `ota_update` task runs `OTAUpdater::performUpdate()`, downloading over HTTPS in 4 KB chunks
6. Streams firmware to inactive partition (app0 ↔ app1); `Update.end()` → `esp_ota_set_boot_partition()` verifies the image checksum/SHA-256
7. A restart is scheduled via `ConfigManager::requestRestart()` and the device boots the new partition
8. At the end of `setup()`, `OTAUpdater::confirmRunningImage()` cancels the pending rollback

**Both OTA tasks are created once, by `OTAUpdater::begin()` in `setup()`, and then park on a task notification forever.** They are never deleted. This is load-bearing: their stacks are static BSS buffers (a FreeRTOS stack needs contiguous *internal* SRAM, which is unreliable to allocate once WiFi/mbedTLS have fragmented the heap), and `vTaskDelete()` only queues a task for reclamation by the idle task — so recreating a task on the same `StaticTask_t` before idle has run corrupts the scheduler's lists. A check and an update are mutually exclusive via a single atomic `Activity` state claimed with one compare-exchange.

**Partition Layout** (4MB flash):
```
app0 (1,856 KB) - Primary firmware partition
app1 (1,856 KB) - OTA target partition
otadata (8 KB)  - Boot partition selector with rollback support
nvs (20 KB)     - Configuration storage
spiffs (256 KB) - Web assets (future use)
coredump (64 KB)- Crash diagnostics
```

**Current firmware**: ~988KB (53% of partition) - plenty of room for growth.

### Key API Methods

**OTAUpdater Class**:
```cpp
// Create the two parked worker tasks. Call from setup(), before the web server.
static void begin();

// Cancel the pending rollback. Call at the END of setup(): a new image that
// crashes during init should still roll back, but any later restart must not.
static void confirmRunningImage();

// Check GitHub for updates (blocking; runs on the ota_check task)
static bool checkForUpdate(const char* owner, const char* repo, FirmwareInfo& info);

// Non-blocking entry points used by the HTTP routes
static bool startBackgroundCheck(const char* owner, const char* repo);
static CheckState getCheckResult(FirmwareInfo& infoOut);
static bool startBackgroundUpdateFromLatestCheck(Config::ConfigManager& config);
static UpdateState getUpdateProgress(int& percentOut, size_t& bytesOut, String& errorOut);

// True only if info.version is strictly newer than FIRMWARE_VERSION
static bool isUpdateAvailable(const FirmwareInfo& info);

// Confirm successful boot (disables rollback)
static bool confirmBoot();

// Check if running unconfirmed update (tests ESP_OTA_IMG_PENDING_VERIFY)
static bool hasUnconfirmedUpdate();

// Memory safety check — INTERNAL SRAM only; esp_get_free_heap_size() includes
// PSRAM on this board and would pass unconditionally.
static bool hasEnoughMemory();

// performUpdate() is private: it takes an arbitrary URL, so only the worker
// task may call it, with a URL that came from the device's own GitHub check.
```

### Web API Endpoints

- `POST /api/ota/check` - Start a background check (202, or 409 if busy)
- `GET /api/ota/check` - Poll the check: `idle` / `checking` / `done` / `error`. Never exposes the download URL.
- `POST /api/ota/update` - Install the release found by the last check. Takes no URL by design.
- `GET /api/ota/update` - Poll the update: `idle` / `downloading` (+`percent`, `bytes`) / `success` / `error` (+`error`)
- `GET /api/ota/status` - Current version, partition, memory info
- `POST /api/ota/confirm` - Confirm successful boot (normally redundant; `confirmRunningImage()` already ran at boot)

### Configuration

Edit `src/OTAConfig.h`:
```cpp
#define OTA_GITHUB_OWNER "your-username"
#define OTA_GITHUB_REPO "untitled"
#define FIRMWARE_VERSION "v1.0.0"
```

### Creating a Release

Normally handled by `.github/workflows/release.yml` on a pushed tag. Manually:

```bash
# Build firmware
pio run -e adafruit_qtpy_esp32s2

# Create GitHub release. The asset MUST be named exactly firmware.bin.
gh release create v1.0.0 \
  .pio/build/adafruit_qtpy_esp32s2/firmware.bin \
  -t "Version 1.0.0" \
  -n "Release notes here"
```

The device looks for the asset named exactly `firmware.bin` (`OTA_FIRMWARE_ASSET`).
It deliberately does *not* pick the first asset ending in `.bin`: a release that
also carried a filesystem image, a bootloader blob, or a build for another board
would otherwise have one of those flashed as the application. Such an image can
still pass `esp_ota_set_boot_partition()`'s verification — it is a valid image,
just not one this board can run — and would boot-loop the device into needing
USB recovery. The tag must be `vMAJOR.MINOR.PATCH`, since the device compares
versions by ordering and ignores unparseable tags.

### Safety Features

1. **HTTPS with Certificate Verification**: Uses esp_crt_bundle (Mozilla CA certificates). Redirects are followed only when they stay on `https` — the `https://github.com/` host allowlist covers just the first hop, so the CDN hop that carries the image is checked separately.
2. **Size Verification**: Checks expected vs actual download size; `esp_ota_set_boot_partition()` verifies the image checksum/SHA-256 before the partition becomes bootable
3. **Exact Asset Name**: Only `firmware.bin` is ever flashed
4. **Version Ordering**: Only a strictly newer release is offered or installed, so a `git describe` dev build is never handed a downgrade
5. **Progressive Streaming**: 4KB chunks minimize memory usage
6. **Automatic Rollback**: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, so a newly flashed image boots as `ESP_OTA_IMG_PENDING_VERIFY` and the bootloader reverts on the *next* reset unless the app confirms it. `confirmRunningImage()` does that at the end of `setup()` — late enough that a crash during init still rolls back, early enough that no automatic restart path (low-heap guard, WiFi force-restart, watchdog, power cycle) can revert a working update.
7. **No Client-Supplied URL**: `POST /api/ota/update` carries no URL; the device flashes only what its own check of the compiled-in owner/repo found
8. **Memory Check**: Requires a minimum free *internal* heap and a minimum largest contiguous internal block before starting OTA

### Memory Requirements

- **Download overhead**: ~64KB heap during OTA
- **Available RAM**: 320KB internal SRAM + 2MB PSRAM. Measure OTA headroom against *internal* SRAM only: `esp_get_free_heap_size()` / `ESP.getFreeHeap()` sum both (`CONFIG_SPIRAM_USE_MALLOC=y`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0`), and the allocations that fail under pressure — task stacks, lwIP/WiFi structures, DMA buffers, the mbedTLS working set — are internal-only. Both the OTA gate and the network task's low-heap restart guard use `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`.
- **Static BSS cost**: 19 KB for the two permanently-resident OTA task stacks (7 KB check + 12 KB update)
- **Flash write**: Direct streaming to partition (no buffering)

### Troubleshooting

**Update check fails**:
- Verify `OTA_GITHUB_OWNER` and `OTA_GITHUB_REPO` in OTAConfig.h
- Ensure device has internet connection
- Check GitHub release exists with .bin file

**Update download fails**:
- Verify stable WiFi connection
- Check free heap > 64KB (`/api/ota/status`)
- Ensure firmware size < 1,856KB

**Device boots into old firmware**:
- New firmware failed to start (check serial logs)
- Automatic rollback activated (otadata partition)
- Flash corruption (rare)

### Documentation

Complete documentation in `docs/` directory:
- `docs/OTA_FIRMWARE_UPDATES.md` - Technical guide (35KB, 7 sections)
- `docs/OTA_QUICK_START.md` - 5-minute integration guide
- `docs/OTA_IMPLEMENTATION_SUMMARY.md` - Implementation checklist
- `OTA_REFERENCE_CARD.md` - API quick reference
- `examples/OTA_INTEGRATION_EXAMPLE.cpp` - 7 usage examples

## Known Constraints

- **PSRAM**: ~2 MB on board (verified on-device at `src/Network.cpp:555` as `psram_size 2094735`). Task stacks, FreeRTOS / lwIP / WiFi / mbedTLS / DMA allocations remain internal-SRAM-only per the *Memory requirements* paragraph above; PSRAM is available for non-real-time allocations but the firmware does not currently use it for any.
- **RAM budget**: ~320KB internal SRAM available (of which ~56KB used, 17.2%); ~2MB PSRAM additional
- **Flash budget**: 4MB available, current usage ~1MB (25%)
- **Stack size**: Sensor Monitor task has 6KB stack, Network task has 8KB stack (both HWM-driven; periodic "stack HWM" lines log the live measurement)
- **Single-core**: ESP32-S2 has only one core (unlike dual-core ESP32)
- **JSON document**: `JsonDocument` on the handler's stack frame; variable-length data via the ArduinoJson default allocator (`heap_caps_malloc` / `free` on ESP32), freed at handler return. The document object itself MUST NOT be heap-allocated (`make_unique<JsonDocument>` / `new JsonDocument` are forbidden in route handlers).
- **Sensor data**: Temperature range -40°C to +125°C, humidity 0-100% RH

## Device Naming

The project is named "Klima-Control" for temperature control. Device IDs follow format `klima-AABBCC` where AABBCC is the last 3 bytes of MAC address. The mDNS hostname removes the dash: `klima-aabbcc.local`.

The status LED provides visual feedback driven by the `LedState` values at `src/StatusLed.h:17-23`:
- **`OFF`**: LED dark
- **`ON`**: MQTT publish progress gradient (green when freshly published, red just before publish) — *not* a steady "green = normal" indicator
- **`STARTUP`**: slow dark-blue blink during boot and WiFi association
- **`TRANSMIT_DATA`**: brief near-white flash during an MQTT publish
- **`ERROR`**: solid red for fatal init errors (e.g. mutex allocation failure)

There is no AP-mode LED state and no yellow "active measurement" state in the shipped enum.
