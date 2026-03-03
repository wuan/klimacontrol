# Klima-Control

ESP32-based temperature and humidity controller with web interface for monitoring and controlling environmental conditions. Runs on the Adafruit QT Py ESP32-S2 with SHT4x sensors for precise environmental monitoring.

## Features

- **Real-time monitoring** - Temperature and humidity readings with precise SHT4x sensors
- **Temperature control** - PID-based temperature control with configurable target
- **Web interface** - Full control and monitoring from any device on your network
- **MQTT integration** - Publish sensor data and control commands via MQTT
- **Data logging** - Historical sensor data for trend analysis
- **Timers & Alarms** - Schedule temperature control actions or notifications
- **OTA updates** - Update firmware over WiFi from GitHub releases
- **Easy setup** - Captive portal for WiFi configuration
- **mDNS discovery** - Access device at `klima-XXXXXX.local`
- **Visual feedback** - Status LED indicates device state (green=normal, yellow=measuring, blue=AP mode, red=error)

## Hardware

**Primary board**: Adafruit QT Py ESP32-S2 (no PSRAM)

| Specification | Value |
|---------------|-------|
| MCU | ESP32-S2 (single-core @ 240MHz) |
| RAM | 320KB |
| Flash | 4MB |
| Sensor | SHT4x (I2C) |
| Sensor range | Temperature: -40°C to +125°C, Humidity: 0-100% RH |
| Sensor precision | ±0.1°C, ±1.5% RH |
| I2C pins | SDA: GPIO 8, SCL: GPIO 9 |

## Getting Started

### Build & Upload

```bash
# Build for ESP32-S2
pio run -e adafruit_qtpy_esp32s2

# Upload to device
pio run -e adafruit_qtpy_esp32s2 -t upload

# Monitor serial output (with exception decoder)
pio device monitor
```

> **Exception Decoder**: The serial monitor runs with the `esp32_exception_decoder` filter enabled (configured via `monitor_filters` in `platformio.ini`). This automatically decodes ESP32 crash stack traces — addresses in exception/backtrace output are resolved to function names and source file line numbers using the debug ELF file. The firmware is built with `build_type = debug` to include debug symbols required for decoding.

### Initial Setup

1. Power on the device
2. The device enters AP mode and broadcasts WiFi network `klima-XXXXXX` (where XXXXXX is derived from MAC address)
3. Connect to this network (default IP: 192.168.4.1)
4. A captive portal opens automatically - enter your WiFi credentials
5. The device restarts and connects to your network
6. Access the web interface at `klima-XXXXXX.local` or check your router for the IP
7. Configure temperature control settings in the web interface

## Web Interface

| Page | Description |
|------|-------------|
| Control | Monitor current temperature/humidity and set temperature control |
| Settings | Configure WiFi, device name, MQTT, target temperature, and control parameters |
| Timers | Set countdown timers or daily alarms for temperature control actions |
| About | Device information and diagnostics |

## REST API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device status, current temperature, humidity, and control state |
| `/api/temperature` | GET | Current temperature and target settings |
| `/api/temperature/target` | POST | Set target temperature |
| `/api/control/enable` | POST | Enable/disable temperature control |
| `/api/sensors` | GET | Detailed sensor information |
| `/api/settings/device` | POST | Update device name and settings |
| `/api/settings/factory-reset` | POST | Factory reset (erases all stored config) |
| `/api/ota/check` | GET | Check for firmware updates on GitHub |
| `/api/ota/update` | POST | Download and install firmware update |
| `/api/ota/status` | GET | Current OTA status and firmware version |

## Configuration

All settings are persisted in ESP32 NVS (Non-Volatile Storage):

- **WiFi Config**: SSID, password, connection status
- **Device Config**: Device name, device ID, sensor I2C address
- **Temperature Config**: Target temperature, control enabled flag
- **MQTT Config**: Broker address, port, topics (if enabled)

## Project Structure

```
src/
  main.cpp                 # Entry point and device initialization
  Config.h/cpp             # NVS persistence and configuration management
  Network.h/cpp            # WiFi, mDNS, NTP, and webserver
  SensorController.h/cpp   # Sensor management and temperature control
  SensorDataLogger.h/cpp   # Historical data logging
  WebServerManager.h/cpp   # Web interface and REST API
  MqttClient.h/cpp         # MQTT connectivity
  StatusLed.h/cpp          # Status LED control
  TimerScheduler.h/cpp     # Timer and alarm management
  TouchController.h/cpp    # Capacitive touch input
  OTAUpdater.h/cpp         # Over-the-air firmware updates
  CaptivePortal.h/cpp      # WiFi setup captive portal
  sensor/
    Sensor.h               # Sensor base class
    SHT4x.h/cpp           # SHT4x temperature/humidity sensor
    DeviceSensor.h/cpp    # Device sensor abstraction
  task/
    SensorMonitor.h/cpp   # Sensor reading FreeRTOS task
data/
  control.html             # Main control page
  settings.html            # Settings page
  timers.html              # Timer management
  about.html               # Device information
tests/
  test_status_led/        # Status LED tests
```

## Architecture

- **Single-core design**: Optimized for ESP32-S2's single core
- **FreeRTOS tasks**:
  - Network Task - handles WiFi, HTTP, mDNS, NTP, and webserver
  - Sensor Monitor Task - reads sensors and updates temperature control
- **Thread-safe**: Direct method calls and shared data with proper synchronization
- **Persistent storage**: All settings stored in ESP32 NVS
- **Real-time operation**: Sensors read every second, temperature control updates continuously

## Temperature Control

The temperature control system uses:
- **PID algorithm** - Calculates control output based on temperature error
- **Sensor averaging** - Multiple sensor readings averaged for accuracy
- **Data logging** - All measurements and control states logged for analysis
- **Configurable targets** - Set any temperature within sensor range

## MQTT Integration

Optional MQTT support for:
- Publishing current temperature and humidity
- Publishing control state and target temperature
- Subscribing to commands to enable/disable control
- Remote monitoring and integration with home automation systems

## Over-the-Air (OTA) Updates

Firmware updates from GitHub releases:
- Checks for new releases automatically
- Downloads .bin files directly from GitHub
- Safe automatic rollback if update fails
- Zero downtime updates (except brief restart)

See [OTA Documentation](docs/OTA_FIRMWARE_UPDATES.md) for detailed setup.

## Development

### Run Tests

```bash
# Run all tests
pio test -e native

# Run specific test suite
pio test -e native -f test_status_led

# Verbose output
pio test -e native -v
```

### Test Coverage

To track test coverage, install `lcov`:
```bash
# macOS
brew install lcov

# Ubuntu
sudo apt-get install lcov
```

Coverage report is automatically generated after running tests.

## Memory Management

- **No raw pointers**: All dynamic memory managed via `std::unique_ptr`
- **RAII pattern**: Resources automatically released when out of scope
- **RAM budget**: ~320KB available, currently using ~56KB (17.2%)
- **Flash budget**: 4MB available, currently using ~1MB (25%)

## Constraints

- **No PSRAM**: ESP32-S2 has no PSRAM - memory usage is strictly limited
- **Single-core**: All tasks run on one core, optimize for responsiveness
- **Stack size**: Sensor Monitor task has 8KB stack, Network task has 10KB stack
- **JSON buffer**: 512 bytes for API responses

## License

MIT
