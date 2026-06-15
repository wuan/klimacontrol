# mqtt-integration Specification (delta)

## Purpose

The mqtt-integration spec already mandates that sensor measurements
are published under `{prefix}/{measurement_type}`. This delta makes
explicit that the same rule applies to device-internal measurements
produced by `DeviceSensor` (RSSI, chip temperature, free heap,
uptime, and — as of this change — largest free block), so the new
`LargestFreeBlock` measurement follows the same publish path with
no new code or topic shape.

## ADDED Requirements

### Requirement: Device-internal measurements are published

The MQTT publish path SHALL publish every measurement produced by
`DeviceSensor` (the synthetic sensor that reports RSSI, chip
temperature, free heap, uptime, and largest free block) on the
same schedule and with the same payload shape as measurements
produced by physical I2C sensors. The topic for each
`DeviceSensor` measurement SHALL be
`{prefix}/<measurement_type_label>` and the payload SHALL be the
same JSON object documented in the
"Publish topic format and payload" requirement.

#### Scenario: Largest free block is published to MQTT

- **WHEN** MQTT is enabled and connected, the device has been up
  for at least 60 seconds, and `DeviceSensor::read()` has produced
  a `LargestFreeBlock` measurement in the most recent cycle
- **THEN** the network task publishes the measurement to
  `{prefix}/largest_free_block` with a JSON payload of the form
  `{"time":<epoch>,"value":<kb>,"unit":"kB","sensor":"ESP32","calculated":false}`

#### Scenario: Topic label is stable

- **WHEN** the firmware is built
- **THEN** `Sensor::measurementTypeLabel(MeasurementType::LargestFreeBlock)`
  equals the string `"largest_free_block"`, so the MQTT topic
  suffix does not change between firmware versions
