[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=oetztal_ledz&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=oetztal_ledz)
[![Bugs](https://sonarcloud.io/api/project_badges/measure?project=oetztal_ledz&metric=bugs)](https://sonarcloud.io/summary/new_code?id=oetztal_ledz)
[![Code Smells](https://sonarcloud.io/api/project_badges/measure?project=oetztal_ledz&metric=code_smells)](https://sonarcloud.io/summary/new_code?id=oetztal_ledz)
[![Duplicated Lines (%)](https://sonarcloud.io/api/project_badges/measure?project=oetztal_ledz&metric=duplicated_lines_density)](https://sonarcloud.io/summary/new_code?id=oetztal_ledz)

# ledz

ESP32-based LED controller with web interface for WS2812B/NeoPixel LED strips.

## Features

- **15 LED shows** - Rainbow, Fire, Wave, Starlight, Mandelbrot, and more
- **Web interface** - Control from any device on your network
- **Presets** - Save and recall up to 8 complete configurations
- **Touch control** - Capacitive touch pins to load presets without WiFi
- **Timers & Alarms** - Schedule shows with countdown timers or daily alarms
- **OTA updates** - Update firmware over WiFi from GitHub releases
- **Easy setup** - Captive portal for WiFi configuration

## Hardware

**Supported board**: Adafruit QT Py ESP32-S3 (no PSRAM)

| Specification | Value |
|---------------|-------|
| LED type | WS2812B / NeoPixel |
| Max LEDs | 300 (configurable) |
| LED pin | GPIO 39 (onboard) or GPIO 35 (external) |

## Getting Started

### Build & Upload

```bash
pio run -e adafruit_qtpy_esp32s3_nopsram -t upload
```

### Initial Setup

1. Power on the device
2. Connect to the WiFi network `ledz-XXXXXX` (where XXXXXX is the device ID)
3. A captive portal opens automatically, or navigate to `192.168.4.1`
4. Enter your WiFi credentials
5. The device restarts and connects to your network
6. Access the web interface at `ledzxxxxxx.local` or check your router for the IP

## Web Interface

| Page | Description |
|------|-------------|
| Control | Select shows, adjust parameters, manage presets |
| Timers | Set countdown timers and daily alarms |
| Settings | Configure WiFi, brightness, LED count, OTA updates |
| About | Device information and diagnostics |

## LED Shows

| Show | Description |
|------|-------------|
| Solid | Static color or multi-color sections (flags) |
| Rainbow | Cycling rainbow spectrum |
| Fire | Realistic fire simulation |
| Wave | Propagating sine-wave patterns |
| Starlight | Twinkling star effect |
| ColorRanges | Multi-color gradient sections |
| TwoColorBlend | Smooth gradient between two colors |
| ColorRun | Running color animation |
| Jump | Bouncing light effects |
| TheaterChase | Marquee-style chase |
| Stroboscope | Flashing strobe light |
| MorseCode | Text as blinking morse code |
| Chaos | Chaotic logistic map patterns |
| Mandelbrot | Fractal zoom visualization |

## Timers

- **Countdown timers** - Turn off or load a preset after a duration
- **Daily alarms** - Recurring triggers at a specific time each day
- Up to 4 concurrent timers
- Actions: Turn off LEDs or load a saved preset

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/show` | POST | Change show with JSON parameters |
| `/api/brightness` | POST | Set brightness (0-255) |
| `/api/status` | GET | Current show and device status |
| `/api/presets` | GET | List saved presets |
| `/api/presets` | POST | Save a preset |
| `/api/timers` | GET | List active timers |
| `/api/timers/countdown` | POST | Set countdown timer |
| `/api/timers/alarm` | POST | Set daily alarm |
| `/api/touch` | GET | Touch config and current values |
| `/api/touch` | POST | Update touch settings |
| `/api/ota/check` | GET | Check for firmware updates |
| `/api/ota/update` | POST | Install firmware update |

## Project Structure

```
src/
  main.cpp              # Entry point
  Config.h/cpp          # NVS persistence
  Network.h/cpp         # WiFi, mDNS, NTP
  WebServerManager.cpp  # Web interface & API
  ShowController.cpp    # Show management
  ShowFactory.cpp       # Show creation
  TimerScheduler.cpp    # Timer system
  TouchController.cpp   # Capacitive touch input
  OTAUpdater.cpp        # Firmware updates
  show/                 # LED show implementations
  strip/                # LED hardware abstraction
data/
  control.html          # Main control page
  settings.html         # Settings page
  timers.html           # Timer scheduler
docs/
  SHOW_PARAMETERS.md    # Show configuration guide
  OTA_FIRMWARE_UPDATES.md
```

## Architecture

- **Dual-core**: Network tasks on Core 0, LED rendering on Core 1
- **100Hz refresh**: Smooth animations at 10ms cycle time
- **Thread-safe**: FreeRTOS queues for inter-core communication
- **Persistent config**: All settings stored in ESP32 NVS

## Development

### Run Tests

```bash
pio test -e native
```

### Test Coverage

To track test coverage, you need `lcov` installed (on macOS: `brew install lcov`).
The coverage report is automatically generated after running tests if the `--coverage` flag is present in `platformio.ini`.

1. Run tests:
   ```bash
   pio test -e native
   ```
2. Open the report:
   ```bash
   open coverage_report/index.html
   ```

### Web Assets

Web files in `data/` are automatically minified and gzip-compressed into C++ header files during the build process. No manual steps required.

## License

MIT
