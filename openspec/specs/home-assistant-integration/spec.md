# home-assistant-integration Specification

## Purpose
Defines how the device presents itself to Home Assistant over MQTT: the
opt-in retained discovery payload, the entity mapping onto the device's
published state, the availability signal, and the republish and removal
lifecycle tied to the configuration toggle.

## Requirements

### Requirement: Discovery is opt-in and retained

The firmware SHALL publish Home Assistant MQTT discovery only when
`MqttConfig.ha_discovery` is enabled and the MQTT client is connected. The
configuration message SHALL be published with the retain flag set, so Home
Assistant receives it even when it subscribes after the device connected.
When `ha_discovery` is disabled the firmware SHALL publish nothing to the
discovery topic.

#### Scenario: Discovery enabled

- **WHEN** `ha_discovery` is enabled and the MQTT client connects
- **THEN** the firmware SHALL publish a retained discovery configuration message

#### Scenario: Discovery disabled

- **WHEN** `ha_discovery` is disabled
- **THEN** no configuration message SHALL be published to the discovery topic

### Requirement: Device discovery uses a single retained message

The firmware SHALL use Home Assistant device discovery: one retained
configuration message on `homeassistant/device/<device_id>/config`
describing the device and every entity it exposes in a single payload. The
message SHALL be published in full and SHALL NOT exceed the MQTT TX buffer.

#### Scenario: One message describes everything

- **WHEN** discovery is published
- **THEN** a single retained message on `homeassistant/device/<device_id>/config` SHALL describe the device and all of its entities

#### Scenario: Payload is not truncated

- **WHEN** the discovery payload is built with the longest permitted device name and topic prefix
- **THEN** the encoded payload, topic and MQTT framing SHALL fit within the MQTT TX buffer

### Requirement: Entity mapping

The discovery message SHALL expose a climate entity, sensors for relative
humidity, dew point and PID output, and a binary sensor for confirmed
heating. Every entity SHALL carry a unique ID and SHALL be associated with
the device.

#### Scenario: Thermostat entity

- **WHEN** Home Assistant processes the discovery message
- **THEN** the climate entity SHALL use `{prefix}/temperature` as its current temperature, `{prefix}/target_temperature` as its target temperature, and `{prefix}/control_enabled` as its mode, reporting `heat` when control is enabled and `off` when disabled

#### Scenario: Sensor entities

- **WHEN** Home Assistant processes the discovery message
- **THEN** it SHALL create a relative-humidity sensor on `{prefix}/relative humidity`, a dew-point sensor on `{prefix}/dew point`, and a PID-output sensor on `{prefix}/control_output` reporting percent

#### Scenario: Heating binary sensor

- **WHEN** Home Assistant processes the discovery message
- **THEN** it SHALL create a heating binary sensor on `{prefix}/heating` that is on when heating is confirmed

### Requirement: Availability follows the device connection

The discovery message SHALL declare `{prefix}/status` as the availability
topic, with `online` meaning available and `offline` meaning unavailable,
so entities become unavailable when the device disconnects.

#### Scenario: Device goes away

- **WHEN** the device loses its broker connection without a clean disconnect
- **THEN** the broker SHALL publish `offline` on `{prefix}/status` and Home Assistant SHALL mark the entities unavailable

#### Scenario: Device comes back

- **WHEN** the device connects to the broker
- **THEN** it SHALL publish `online` on `{prefix}/status` and Home Assistant SHALL mark the entities available

### Requirement: Discovery lifecycle

The firmware SHALL republish the discovery message after every successful
MQTT (re)connect, independently of the measurement publish interval and the
post-boot settle period. When `ha_discovery` is turned off the firmware
SHALL publish an empty retained payload to the discovery topic so the
device is removed.

#### Scenario: Reconnect republishes

- **WHEN** the MQTT client reconnects after a disconnection
- **THEN** the discovery message SHALL be published again

#### Scenario: Disabling removes the device

- **WHEN** `ha_discovery` is turned off while the client is connected
- **THEN** an empty retained payload SHALL be published to the discovery topic and Home Assistant SHALL remove the device
