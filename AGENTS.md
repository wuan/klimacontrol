# AGENTS.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Klima-Control** is an ESP32-based temperature controller with web interface for monitoring and controlling temperature and humidity. It runs on the Adafruit QT Py ESP32-S2 (no PSRAM) and uses SHT4x sensors for precise environmental monitoring.

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
- Sensor data is averaged across all connected sensors
- Data logging for historical analysis

**Control flow**:
1. Sensors are read at configured intervals (default: 1 second)
2. `SensorController::readSensors()` collects and averages data
3. Temperature control algorithm calculates output (PID control)
4. Status LED shows yellow during measurement, green when idle
5. Web interface displays real-time data and control status
6. Sensor data is averaged across multiple sensors for accuracy
7. Temperature control algorithm runs every second
8. All measurements and control states are logged for analysis

**Temperature Control Flow**:
1. Sensors read at configured interval (default: 1 second)
2. `SensorController::readSensors()` collects and validates data
3. Valid readings are averaged and stored
4. Temperature control calculates output using PID algorithm
5. Status LED provides visual feedback during each phase
6. Web interface updates in real-time via API

### Sensor Architecture

**Sensor Management**: The `SensorController` manages multiple sensor instances with automatic averaging and validation.

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

// Reading sensors (automatically averages multiple sensors)
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

The status LED provides visual feedback using the `StatusLed` class:

```cpp
// Set LED states
statusLed.setState(LedState::ON);           // Green - normal operation
statusLed.setState(LedState::MEASURING);   // Yellow - active measurement
statusLed.setState(LedState::BLINK_SLOW); // Blue - AP mode
statusLed.setState(LedState::PULSE);       // Red - error/warning

// Convenience methods
statusLed.setMeasuring();  // Shortcut for yellow during measurement
statusLed.setNormal();     // Shortcut for green normal operation
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
1. User clicks "Check for Updates" in Settings
2. `OTAUpdater::checkForUpdate()` queries GitHub REST API for latest release
3. If update available, shows version, size, and release notes
4. User clicks "Install Update"
5. `OTAUpdater::performUpdate()` downloads .bin file from GitHub over HTTPS
6. Streams firmware to inactive partition (app0 ↔ app1)
7. ESP.restart() boots into new partition
8. Optional: `OTAUpdater::confirmBoot()` disables automatic rollback

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
// Check GitHub for updates
static bool checkForUpdate(const char* owner, const char* repo, FirmwareInfo& info);

// Download and flash firmware
static bool performUpdate(const String& url, size_t size,
                         std::function<void(int, size_t)> onProgress = nullptr);

// Confirm successful boot (disables rollback)
static bool confirmBoot();

// Check if running unconfirmed update
static bool hasUnconfirmedUpdate();

// Memory safety check
static bool hasEnoughMemory();  // Requires 64KB free heap
```

### Web API Endpoints

- `GET /api/ota/check` - Check for updates from GitHub
- `POST /api/ota/update` - Download and install update
- `GET /api/ota/status` - Current version, partition, memory info
- `POST /api/ota/confirm` - Confirm successful boot

### Configuration

Edit `src/OTAConfig.h`:
```cpp
#define OTA_GITHUB_OWNER "your-username"
#define OTA_GITHUB_REPO "untitled"
#define FIRMWARE_VERSION "v1.0.0"
```

### Creating a Release

```bash
# Build firmware
pio run -e adafruit_qtpy_esp32s3_nopsram

# Create GitHub release with .bin file
gh release create v1.0.0 \
  .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin \
  -t "Version 1.0.0" \
  -n "Release notes here"
```

The device will automatically find the .bin file in the release assets.

### Safety Features

1. **HTTPS with Certificate Verification**: Uses esp_crt_bundle (Mozilla CA certificates)
2. **Size Verification**: Checks expected vs actual download size
3. **Progressive Streaming**: 4KB chunks minimize memory usage
4. **Automatic Rollback**: If new firmware doesn't call `confirmBoot()`, rolls back on next boot
5. **Memory Check**: Requires 64KB free heap before starting OTA

### Memory Requirements

- **Download overhead**: ~64KB heap during OTA
- **Available RAM**: 320KB (5x required minimum)
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

- **No PSRAM**: Adafruit QT Py ESP32-S2 board has no PSRAM - keep memory usage minimal
- **RAM budget**: ~320KB available, current usage ~56KB (17.2%)
- **Flash budget**: 4MB available, current usage ~1MB (25%)
- **Stack size**: Sensor Monitor task has 8KB stack, Network task has 10KB stack
- **Single-core**: ESP32-S2 has only one core (unlike dual-core ESP32)
- **JSON buffer**: 512 bytes for API responses (StaticJsonDocument<512>)
- **Sensor data**: Temperature range -40°C to +125°C, humidity 0-100% RH

## Device Naming

The project is named "Klima-Control" for temperature control. Device IDs follow format `klima-AABBCC` where AABBCC is the last 3 bytes of MAC address. The mDNS hostname removes the dash: `klima-aabbcc.local`.

The status LED provides visual feedback:
- **Green**: Normal operation
- **Yellow**: Active sensor measurement
- **Blue**: AP mode (configuration)
- **Red**: Error or warning condition
