[![Lines of Code](https://sonarcloud.io/api/project_badges/measure?project=wuan_klimacontrol&metric=ncloc)](https://sonarcloud.io/summary/new_code?id=wuan_klimacontrol)
[![Coverage](https://sonarcloud.io/api/project_badges/measure?project=wuan_klimacontrol&metric=coverage)](https://sonarcloud.io/summary/new_code?id=wuan_klimacontrol)
[![Duplicated Lines (%)](https://sonarcloud.io/api/project_badges/measure?project=wuan_klimacontrol&metric=duplicated_lines_density)](https://sonarcloud.io/summary/new_code?id=wuan_klimacontrol)
[![Maintainability Rating](https://sonarcloud.io/api/project_badges/measure?project=wuan_klimacontrol&metric=sqale_rating)](https://sonarcloud.io/summary/new_code?id=wuan_klimacontrol)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=wuan_klimacontrol&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=wuan_klimacontrol)

# Klima-Control

ESP32-based temperature and humidity controller with web interface for monitoring and controlling environmental conditions. Runs on the Adafruit QT Py ESP32-S2 and supports a wide range of I2C sensors for temperature, humidity, pressure, CO2, air quality, light, and VOC monitoring.

## Features

- **Real-time monitoring** - Temperature and humidity readings from a wide range of I2C sensors
- **Temperature control** - PID-based temperature control with configurable target
- **Web interface** - Full control and monitoring from any device on your network
- **MQTT integration** - Publish sensor data and control commands via MQTT
- **Remote syslog** - Forward device logs to a remote syslog server (UDP, RFC 3164)
- **Data logging** - Historical sensor data for trend analysis
- **Timers & Alarms** - Schedule temperature control actions or notifications
- **OTA updates** - Update firmware over WiFi from GitHub releases
- **Easy setup** - Captive portal for WiFi configuration
- **mDNS discovery** - Access device at `klima-XXXXXX.local`
- **Visual feedback** - Status LED indicates device state (solid=on, blink=startup, flash=MQTT transmit)

## Hardware

**Primary board**: Adafruit QT Py ESP32-S2 (no PSRAM)

**Matching Housing**: [3D-printable housing on MakerWorld](https://makerworld.com/de/models/2521186)

| Specification | Value |
|---------------|-------|
| MCU | ESP32-S2 (single-core @ 240MHz) |
| RAM | 320KB |
| Flash | 4MB |
| Sensors | Multiple I2C sensors (auto-detected) |
| I2C pins | SDA: GPIO 8, SCL: GPIO 9 |

## Supported Sensors

All sensors connect via the Stemma QT / I2C port and are automatically detected at startup. Multiple sensors can be connected simultaneously.

| Sensor | Category | Measurements | Calculated Values | I2C Address(es) |
|--------|----------|-----------|---------|-----------------|
| **SHT4x** | Temperature & Humidity | Temperature, Relative Humidity | Dew Point | 0x44, 0x45 |
| **BME680** | Environmental | Temperature, Relative Humidity, Pressure | Dew Point, Sea Level Pressure | 0x76, 0x77 |
| **BMP3xx** | Pressure | Pressure | Sea Level Pressure | 0x76, 0x77 |
| **DPS310** | Pressure | Pressure | Sea Level Pressure | 0x76, 0x77 |
| **SCD4x** | CO2 | CO2 (ppm) | | 0x62 |
| **SGP40** | Air Quality | VOC Index | — | 0x59 |
| **BH1750** | Light | Illuminance (lux) | — | 0x23, 0x5C |
| **TSL2591** | Light | Illuminance (lux) | — | 0x29 |
| **VEML7700** | Light | Illuminance (lux) | — | 0x10 |
| **PM25 AQI** | Air Quality | PM1.0 / PM2.5 / PM10 concentration (µg/m³), particle counts | — | 0x12 |

> **Note**: The SGP40 VOC sensor requires temperature and relative humidity readings from another sensor (e.g. SHT4x) to compute a calibrated VOC index.

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
2. The device enters AP mode and broadcasts WiFi network `Klima-XXXXXX` (where XXXXXX is derived from MAC address)
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

The project includes an [OpenAPI specification](docs/api/klimacontrol-api.yaml) documenting the primary API endpoints with request/response schemas. Interactive HTML documentation is deployed to GitHub Pages.

> **Note**: The OpenAPI spec currently covers the primary endpoints. Additional endpoints (MQTT, OTA, sensors, settings) are documented in the source code.

### Key Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Device status, current temperature, humidity, and control state |
| `/api/temperature/target` | POST | Set target temperature |
| `/api/control/enable` | POST | Enable/disable temperature control |
| `/api/sensors` | GET | Detailed sensor information |

## Configuration

All settings are persisted in ESP32 NVS (Non-Volatile Storage):

- **WiFi Config**: SSID, password, connection status
- **Device Config**: Device name, device ID, sensor I2C address
- **Temperature Config**: Target temperature, control enabled flag
- **MQTT Config**: Broker address, port, topics (if enabled)
- **Syslog Config**: Syslog server address, port, enabled flag

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
  SyslogOutput.h/cpp       # UDP syslog log forwarding
  Log.h                    # Project-level logging macros
  StatusLed.h/cpp          # Status LED control
  TimerScheduler.h/cpp     # Timer and alarm management
  TouchController.h/cpp    # Capacitive touch input
  OTAUpdater.h/cpp         # Over-the-air firmware updates
  CaptivePortal.h/cpp      # WiFi setup captive portal
  sensor/
    Sensor.h               # Sensor base class and measurement types
    SHT4x.h/cpp           # Temperature & humidity sensor
    BME680.h/cpp          # Environmental sensor (temp, humidity, pressure)
    BMP3xx.h/cpp          # Pressure sensor
    DPS310.h/cpp          # Pressure sensor
    SCD4x.h/cpp           # CO2, temperature & humidity sensor
    SGP40.h/cpp           # VOC index sensor
    BH1750.h/cpp          # Ambient light sensor
    TSL2591.h/cpp         # High-dynamic-range light sensor
    VEML7700.h/cpp        # Ambient light sensor
    PM25.h/cpp            # Particulate matter / air quality sensor
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

## Syslog

Device logs can be forwarded to a remote syslog server via UDP (RFC 3164). Configure the syslog target in the web interface under Settings or via the `/api/syslog` endpoint.

- **Protocol**: UDP syslog (RFC 3164)
- **Facility**: user (1)
- **Hostname**: uses the device's mDNS hostname (e.g. `klima-aabbcc`)
- **Severity mapping**: E=error(3), W=warning(4), I=info(6), D=debug(7)
- **Zero overhead when disabled**: no UDP socket allocated, inline `isActive()` check

## Over-the-Air (OTA) Updates

Firmware updates from GitHub releases:
- Checks for new releases automatically
- Downloads .bin files directly from GitHub
- Safe automatic rollback if update fails
- Zero downtime updates (except brief restart)

See [OTA Documentation](docs/OTA_FIRMWARE_UPDATES.md) for detailed setup.

## Development

### Specifications

The project includes two types of specifications:

1. **OpenSpec Documents** in `specs/` - Markdown-based system specifications
2. **OpenAPI Specifications** in `docs/api/` - API interface definitions

#### OpenSpec Validation

The OpenSpec documents in the `specs/` directory define the system architecture, requirements, and behavior. These are validated using the official OpenSpec CLI:

- GitHub Actions automatically validates all OpenSpec documents on push/pull request
- HTML documentation is generated from the Markdown specs
- Validation includes proper formatting, cross-references, and requirement syntax

To validate locally:
```bash
openspec validate --specs
```

#### API Documentation

The project includes OpenAPI specification files in the `docs/api/` directory. When you push changes to these files:

1. GitHub Actions automatically validates the OpenAPI specs
2. HTML documentation is generated using Redoc
3. The documentation is deployed to GitHub Pages

To validate OpenAPI specs locally:
```bash
spectral lint docs/api/*.yaml
```

To generate HTML documentation locally:
```bash
redoc-cli bundle docs/api/klimacontrol-api.yaml -o docs/api/index.html
```

#### API Documentation

The project includes OpenAPI specification files in the `docs/api/` directory. When you push changes to these files:

1. GitHub Actions automatically validates the OpenAPI specs
2. HTML documentation is generated using Redoc
3. The documentation is available as a downloadable artifact

To view the API documentation:
1. Go to the Actions tab in GitHub
2. Find the latest "OpenAPI Validation and Documentation" workflow run
3. Download the "api-documentation" artifact
4. Open the HTML files in your browser

You can also generate the documentation locally:
```bash
# Install required tools
npm install -g redoc-cli

# Generate HTML from OpenAPI spec
redoc-cli bundle docs/api/klimacontrol-api.yaml -o docs/api/index.html
```

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
