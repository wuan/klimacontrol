# Status LED Specification

## Purpose

Define visual feedback system using built-in NeoPixel LED.

## Requirements

### Hardware
1. **S9.1** SHALL use built-in NeoPixel LED on QT Py ESP32-S2
2. **S9.2** SHALL use NEO_GRB format (Red, Green, Blue)

### LED States
3. **S9.3** SHALL define LedState enum: OFF, ON, BLINK_SLOW, BLINK_FAST, PULSE, MEASURING

### StatusLed Class
4. **S9.4** SHALL provide StatusLed class
5. **S9.5** SHALL provide setState(LedState) method
6. **S9.6** SHALL provide update() method for timing
7. **S9.7** SHALL provide show() method to update LED

### Color Mapping
8. **S9.8** ON SHALL display GREEN (0, 255, 0)
9. **S9.9** MEASURING SHALL display YELLOW (255, 255, 0)
10. **S9.10** BLINK_SLOW SHALL display BLUE (0, 0, 255) with 2000ms period
11. **S9.11** PULSE SHALL display RED (255, 0, 0) with sine wave brightness

### Convenience Methods
12. **S9.12** SHALL provide setNormal() for ON
13. **S9.13** SHALL provide setMeasuring() for MEASURING
14. **S9.14** SHALL provide setApMode() for BLINK_SLOW
15. **S9.15** SHALL provide setError() for PULSE

### Usage Rules
16. **S9.16** Normal operation SHALL use ON (green)
17. **S9.17** Sensor measurement SHALL use MEASURING (yellow)
18. **S9.18** AP mode SHALL use BLINK_SLOW (blue)
19. **S9.19** Error condition SHALL use PULSE (red)

### Timing
20. **S9.20** update() SHALL be called periodically (>= every 50ms)
21. **S9.21** SHALL use millis() for timing

## Scenarios

### Scenario S9-A: Normal Operation
Given device operational,
Then LED SHALL display solid GREEN

### Scenario S9-B: Sensor Measurement
Given sensor read in progress,
Then LED SHALL display solid YELLOW

### Scenario S9-C: AP Mode
Given device in AP mode,
Then LED SHALL blink BLUE with 2000ms period (1s on, 1s off)

### Scenario S9-D: Error Condition
Given repeated sensor failures,
Then LED SHALL pulse RED with sine wave pattern

### Scenario S9-E: WiFi Connection Sequence
Given booting in STA mode,
Then:
- Initial: BLINK_FAST (white) - connecting
- Success: ON (green)
- Failure 3x: BLINK_SLOW (blue) - AP mode
