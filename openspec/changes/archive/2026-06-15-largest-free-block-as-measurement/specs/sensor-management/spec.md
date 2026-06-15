# sensor-management Specification (delta)

## Purpose

The sensor-management spec already enumerates the supported sensor
drivers and the `Sensor::Measurement` model. This delta adds a new
device-internal measurement type — `LargestFreeBlock` — to the
documented set, so the `DeviceSensor`'s heap-fragmentation reading
is a first-class measurement like the existing `FreeHeap`,
`Rssi`, `Uptime`, and `Time` types.

## ADDED Requirements

### Requirement: Largest free block is a measurement type

The firmware SHALL include `LargestFreeBlock` in the
`Sensor::MeasurementType` enum, and the value SHALL be sourced
from `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` (the size
of the largest contiguous free block in the 8-bit-capable heap,
in bytes). `measurementTypeLabel(LargestFreeBlock)` SHALL return
`"largest_free_block"` and `measurementTypeUnit(LargestFreeBlock)`
SHALL return `"kB"`. The `DeviceSensor` SHALL list
`LargestFreeBlock` in `providesMeasurements()` and SHALL push a
measurement of that type in `read()` on the same cadence as the
other device-internal metrics.

#### Scenario: Label and unit for the new type

- **WHEN** a caller invokes `measurementTypeLabel(MeasurementType::LargestFreeBlock)`
- **THEN** the result is the string `"largest_free_block"`

#### Scenario: Unit for the new type

- **WHEN** a caller invokes `measurementTypeUnit(MeasurementType::LargestFreeBlock)`
- **THEN** the result is the string `"kB"` (matching the existing
  `FreeHeap` convention; the value stored in the measurement is the
  raw byte count divided by 1024)

#### Scenario: DeviceSensor declares the new type

- **WHEN** a caller invokes `DeviceSensor::providesMeasurements()`
- **THEN** the returned `TypeSpan` includes `MeasurementType::LargestFreeBlock`
  in its list

#### Scenario: DeviceSensor pushes the measurement on each read

- **WHEN** the firmware is running on the device and
  `DeviceSensor::read()` is called as part of a normal sensor cycle
- **THEN** the returned `SensorReading` contains a measurement of
  type `LargestFreeBlock` whose value (in kB) is the largest free
  8-bit-capable heap block at the moment of the call
