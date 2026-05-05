# Status LED Specification

## Purpose

Define visual feedback system using built-in NeoPixel LED.

## Requirements

### Hardware
1. **S9.1** SHALL use built-in NeoPixel LED on QT Py ESP32-S2
2. **S9.2** SHALL use NEO_GRB format (Red, Green, Blue)

### LED States
3. **S9.3** SHALL define LedState enum: OFF, ON, STARTUP, TRANSMIT_DATA

### StatusLed Class
4. **S9.4** SHALL provide StatusLed class
5. **S9.5** SHALL provide setState(LedState) method
6. **S9.6** SHALL provide update() method for timing
7. **S9.7** SHALL provide show() method to update LED

### Color Mapping
8. **S9.8** ON SHALL display solid color (implementation-defined)
9. **S9.9** STARTUP SHALL blink slowly (1 Hz)
10. **S9.10** TRANSMIT_DATA SHALL flash briefly (MQTT publish indication)

### Usage Rules
11. **S9.11** Normal operation SHALL use ON state
12. **S9.12** Boot sequence SHALL use STARTUP state
13. **S9.13** MQTT transmission SHALL use TRANSMIT_DATA state

### Timing
14. **S9.14** update() SHALL be called periodically
15. **S9.15** SHALL use millis() for timing

## Scenarios

### Scenario S9-A: Normal Operation
Given device operational,
Then LED SHALL display solid color

### Scenario S9-B: Boot Sequence
Given device booting,
Then LED SHALL blink slowly (1 Hz)

### Scenario S9-C: MQTT Transmission
Given MQTT publish in progress,
Then LED SHALL flash briefly
