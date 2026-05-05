# Klima-Control OpenSpec Specifications

This is the index for all OpenSpec specifications for the Klima-Control ESP32 temperature controller project.

## Specifications

| # | Specification | Description |
|---|---------------|-------------|
| 01 | [System Architecture](./01-system-architecture.md) | Core architecture, components, and FreeRTOS task model |
| 02 | [Sensor Management](./02-sensor-management.md) | Sensor interface, discovery, reading, and status tracking |
| 03 | [API Specification](./03-api-specification.md) | REST API endpoints, request/response formats, error handling |
| 04 | [Networking](./04-networking.md) | WiFi connectivity, STA/AP modes, mDNS, captive portal |
| 05 | [OTA Updates](./05-ota-updates.md) | Over-the-air firmware update mechanism |
| 06 | [MQTT Integration](./06-mqtt-integration.md) | MQTT client for sensor data publishing |
| 07 | [Web Interface](./07-web-interface.md) | Web-based user interface and dashboard |
| 08 | [Configuration](./08-configuration.md) | Persistent configuration storage via ESP32 NVS |
| 09 | [Status LED](./09-status-led.md) | Visual feedback system via NeoPixel LED |
| 10 | [Temperature Control](./10-temperature-control.md) | PID temperature control algorithm |

## Overview

Klima-Control is an ESP32-S2 based temperature and humidity monitoring and control system. It features:

- Real-time temperature and humidity sensing via I2C sensors
- Web-based configuration and control interface
- PID-based temperature control
- WiFi connectivity with AP mode for initial setup
- mDNS discovery (klima-aabbcc.local)
- Over-the-air firmware updates from GitHub releases
- MQTT support for sensor data publishing
- Persistent configuration via ESP32 Preferences (NVS)
- Visual status feedback via NeoPixel LED

## Architecture

The system uses a single-core ESP32-S2 with FreeRTOS tasks:

- **Network Task**: Handles WiFi, NTP, mDNS, webserver, and API
- **Sensor Monitor Task**: Reads sensors and updates temperature control state

All sensor data access is thread-safe through the SensorController.

## Getting Started

Start with the [System Architecture](./01-system-architecture.md) specification to understand the overall design, then refer to individual component specifications as needed.
