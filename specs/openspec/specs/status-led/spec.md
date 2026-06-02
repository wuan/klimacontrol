# status-led Specification

## Purpose
TBD - created by archiving change baseline-capabilities. Update Purpose after archive.
## Requirements
### Requirement: Hardware target

The firmware SHALL drive the single built-in NeoPixel on the Adafruit QT Py ESP32-S2 board, using the `NEO_GRB` color order.

#### Scenario: Pixel write

- **WHEN** `StatusLed::show()` is called
- **THEN** exactly one NeoPixel SHALL be updated, with the current color expressed in GRB order

### Requirement: State machine

The LED SHALL be controlled by a `LedState` enum with the values `OFF`, `ON`, `STARTUP`, `TRANSMIT_DATA`. `StatusLed` SHALL provide `setState(LedState)`, `update()` (called periodically from the network task to drive animations), and `show()` (writes to the NeoPixel). The current state SHALL be readable via `getState()`.

#### Scenario: State transition

- **WHEN** `setState(LedState::ON)` is called
- **THEN** subsequent `update()` calls SHALL drive the LED to the solid-on color and `getState()` SHALL return `ON`

### Requirement: State-to-behavior mapping

- `OFF` SHALL leave the LED dark.
- `ON` SHALL display a steady color encoding the MQTT publish progress as a green→red gradient (driven by `setProgress(float)`); when progress is unavailable the color SHALL be plain.
- `STARTUP` SHALL blink the LED at approximately 1 Hz to indicate boot/association is in progress.
- `TRANSMIT_DATA` SHALL flash the LED briefly to indicate an MQTT publish.

#### Scenario: Boot indicator

- **WHEN** the firmware is in the early-boot path before WiFi is associated
- **THEN** the LED SHALL be in `STARTUP` state and SHALL blink at ~1 Hz

#### Scenario: MQTT publish flash

- **WHEN** a measurement is about to be published via MQTT
- **THEN** the network task SHALL call `setState(LedState::TRANSMIT_DATA)`, producing a short visible flash

### Requirement: Periodic update from the network task

The network task SHALL call `statusLed->update()` on each 1-second iteration so blinking/flash timing remains accurate. The LED SHALL use `millis()` as its timing source.

#### Scenario: Cadence guarantee

- **WHEN** the network task is running normally
- **THEN** `StatusLed::update()` SHALL be invoked at least once per second so animations advance correctly

