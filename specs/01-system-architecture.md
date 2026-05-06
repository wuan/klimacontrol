# System Architecture Specification

## Purpose

Define the overall system architecture, hardware platform, component relationships, memory model, and FreeRTOS task structure for Klima-Control.

## Requirements

### Hardware Platform

1. **S1.1** SHALL run on Adafruit QT Py ESP32-S2 hardware platform
2. **S1.2** SHALL use ESP32-S2 single-core processor at 80 MHz
3. **S1.3** SHALL use Stemma QT I2C connector for sensor communication
4. **S1.4** SHALL use built-in NeoPixel LED for status indication

### Memory Constraints

5. **S1.5** SHALL have total RAM budget of approximately 320 KB
6. **S1.6** SHALL have flash usage under 2 MB (current: ~988 KB)
7. **S1.7** SHALL have no PSRAM available on target hardware
8. **S1.8** SHALL maintain minimum 64 KB free heap for OTA operations

### FreeRTOS Task Model

9. **S1.9** SHALL use FreeRTOS for concurrent task execution
10. **S1.10** SHALL have Network Task running `Network::task()`
11. **S1.11** SHALL have Sensor Monitor Task running `SensorMonitor::task()`
12. **S1.12** SHALL configure task watchdog timer with 30-second timeout
13. **S1.13** SHALL trigger panic on task watchdog timeout
14. **S1.14** Network Task SHALL have 10 KB stack size
15. **S1.15** Sensor Monitor Task SHALL have 8 KB stack size

### Task Responsibilities

16. **S1.16** Network Task SHALL handle WiFi connectivity (STA and AP modes)
17. **S1.17** Network Task SHALL handle NTP time synchronization
18. **S1.18** Network Task SHALL handle mDNS service advertisement
19. **S1.19** Network Task SHALL handle webserver and API requests
20. **S1.20** Sensor Monitor Task SHALL read all connected sensors
21. **S1.21** Sensor Monitor Task SHALL update temperature control state
22. **S1.22** Sensor Monitor Task SHALL calculate PID control output
23. **S1.23** Sensor Monitor Task SHALL run at 1-second intervals by default

### Component Ownership Hierarchy

24. **S1.24** `main.cpp` SHALL own the primary setup and initialization
25. **S1.25** `SensorController` SHALL own all sensor instances
26. **S1.26** `Network` SHALL own the `AsyncWebServer` instance
27. **S1.27** `Network` SHALL own the `StatusLed` instance
28. **S1.28** `Network` SHALL own the `WebServerManager` instance
29. **S1.29** `WebServerManager` SHALL have references to Config, Network, SensorController, and SensorMonitor

### Memory Management Rules

30. **S1.30** Memory Management section header (reserved for future use)
31. **S1.31** SHALL use `std::unique_ptr<T>` for all owned resources
32. **S1.32** SHALL transfer ownership using `std::move()`
33. **S1.33** SHALL use references (`T&`) for non-owning access
34. **S1.34** SHALL NEVER use raw pointers for resource ownership
35. **S1.35** SHALL NEVER store raw pointers to owned resources

### Thread Safety

36. **S1.36** SHALL use mutex/semaphore for shared data access
37. **S1.37** SensorController SHALL use dataMutex for measurement access
38. **S1.38** All sensor data access SHALL be thread-safe

## Scenarios

### Scenario S1-A: System Initialization

Given the device powers on for the first time,
When `setup()` is called,
Then:
1. Delay 1000ms for hardware stabilization
2. Initialize logging (ESP_LOGI with "main" tag)
3. `config.begin()` initializes NVS preferences
4. I2C bus (Wire1) initialized with SDA1, SCL1 pins
5. Sensor instances created from configuration string
6. `sensorController.begin()` initializes sensors
7. Network task started
8. Sensor monitor task started
9. Task watchdog timer configured with 30s timeout

### Scenario S1-B: Memory Allocation

Given a new sensor needs to be added,
When `sensorController.addSensor(std::make_unique<Sensor::SHT4x>(addr))` is called,
Then:
- A new SHT4x instance SHALL be created with `std::make_unique`
- Ownership SHALL be transferred to SensorController via `std::move`
- SensorController SHALL store the sensor as `std::unique_ptr<Sensor::Sensor>`
- No raw pointers SHALL be used
