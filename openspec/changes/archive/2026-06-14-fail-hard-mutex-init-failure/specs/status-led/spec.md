## MODIFIED Requirements

### Requirement: State machine

The LED SHALL be controlled by a `LedState` enum with the values `OFF`, `ON`, `STARTUP`, `TRANSMIT_DATA`, `ERROR`. `StatusLed` SHALL provide `setState(LedState)`, `update()` (called periodically from the network task to drive animations), and `show()` (writes to the NeoPixel). The current state SHALL be readable via `getState()`.

#### Scenario: State transition

- **WHEN** `setState(LedState::ON)` is called
- **THEN** subsequent `update()` calls SHALL drive the LED to the solid-on color and `getState()` SHALL return `ON`

#### Scenario: Transition to error state

- **WHEN** `setState(LedState::ERROR)` is called
- **THEN** `getState()` SHALL return `ERROR` and subsequent `update()` calls SHALL drive the LED to solid red

### Requirement: State-to-behavior mapping

- `OFF` SHALL leave the LED dark.
- `ON` SHALL display a steady color encoding the MQTT publish progress as a green→red gradient (driven by `setProgress(float)`); when progress is unavailable the color SHALL be plain.
- `STARTUP` SHALL blink the LED at approximately 1 Hz to indicate boot/association is in progress.
- `TRANSMIT_DATA` SHALL flash the LED briefly to indicate an MQTT publish.
- `ERROR` SHALL display solid red to indicate a fatal init error (e.g. mutex allocation failure in `SensorController`).

#### Scenario: Boot indicator

- **WHEN** the firmware is in the early-boot path before WiFi is associated
- **THEN** the LED SHALL be in `STARTUP` state and SHALL blink at ~1 Hz

#### Scenario: MQTT publish flash

- **WHEN** a measurement is about to be published via MQTT
- **THEN** the network task SHALL call `setState(LedState::TRANSMIT_DATA)`, producing a short visible flash

#### Scenario: Error indicator

- **WHEN** the firmware drives the LED to indicate a fatal init error
- **THEN** the LED SHALL be in `ERROR` state and SHALL display solid red so a human looking at the device can see the cause
