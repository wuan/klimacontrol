# web-interface Specification Delta

## ADDED Requirements

### Requirement: Tuning section on the settings page

The settings page SHALL provide a Tuning section covering the PID gains and the control interval, with its own save action like every other section.

Each field SHALL be labelled with its unit and its permitted range, because a sensible `ki` is a small fraction whose order of magnitude is not obvious and is easy to mistype. The section SHALL additionally display the derived integral time `Ti = Kp / Ki` in seconds, which is the quantity with physical meaning and the one the autotuner's derivation actually produces.

The section SHALL state that these values drive a physical valve, because this is the only settings section whose misconfiguration has a thermal consequence rather than a cosmetic one.

Rejections from the write endpoint SHALL be surfaced with the offending field named, rather than reported as a generic failure, since an all-or-nothing validation that does not say which field failed leaves the user guessing.

#### Scenario: Section rendering

- **WHEN** the settings page loads
- **THEN** a Tuning section SHALL be present as a headed block with its own save action

#### Scenario: Current values shown

- **WHEN** the Tuning section loads
- **THEN** it SHALL show the gains in force and the configured control interval

#### Scenario: Integral time is derived for the user

- **WHEN** `Kp` is `0.5` and `Ki` is `0.0001`
- **THEN** the section SHALL show an integral time of approximately 5000 seconds

#### Scenario: Integral time with no integral action

- **WHEN** `Ki` is zero
- **THEN** the section SHALL indicate that there is no integral action rather than rendering a division by zero

#### Scenario: Physical consequence stated

- **WHEN** the Tuning section is displayed
- **THEN** it SHALL state that these values drive a physical valve

#### Scenario: Rejected field is named

- **WHEN** a save is rejected because `ki` is out of range
- **THEN** the section SHALL report that `ki` was the offending field and SHALL leave the form values as entered

## MODIFIED Requirements

### Requirement: Autotune UI states its current limitations

A run cannot converge unless the device can actually drive its zone: without a conforming actuator assignment the plant does not respond to the relay, so the run ends in a timeout. The UI SHALL state this **when it applies** — when no actuator channel is assigned, or when the assigned channel is refused — so that an expected timeout is not read as a malfunction. When a conforming actuator is assigned, the UI SHALL NOT display the warning, because a run can then converge and a standing warning would be misleading.

The UI SHALL NOT describe acceptance as temporary. Accepted gains are persisted and survive a restart.

#### Scenario: Limitation stated when no actuator is assigned

- **WHEN** the user views the autotune controls with no actuator channel assigned
- **THEN** the UI SHALL state that runs cannot converge until an actuator is assigned

#### Scenario: Limitation stated when the channel is refused

- **WHEN** the user views the autotune controls while the assigned channel is non-conforming
- **THEN** the UI SHALL state that the channel is refused and that a run will end in a timeout

#### Scenario: No warning with a working actuator

- **WHEN** the user views the autotune controls with a conforming actuator assigned
- **THEN** the UI SHALL NOT claim that runs cannot converge

#### Scenario: Timeout is not presented as a fault

- **WHEN** a run ends in a settling or run timeout
- **THEN** the UI SHALL report the reason without implying a malfunction

#### Scenario: Acceptance is described as durable

- **WHEN** derived gains are offered for acceptance
- **THEN** the UI SHALL NOT state that they are lost on restart
- **AND** SHALL indicate that accepting stores them

### Requirement: Settings page sections

The settings page SHALL organize configuration into distinct sections covering at least: Device Name, Elevation, Timezone, I2C Sensors, Tuning, MQTT, E-Paper Display, Syslog, WiFi, Energy, OTA, System.

Sections SHALL be rendered as stacked blocks on a single page, each with its own heading and its own save action, so that saving one section does not submit the others.

The Timezone section SHALL provide a dropdown of common zones whose values are POSIX TZ strings, plus a **Custom** entry revealing a free-text field for any zone not listed. The zone table SHALL live in the page, not in firmware flash, so it can be corrected or extended without a firmware change.

The section SHALL display the current device time as reported by `GET /api/settings/timezone`, so the user can confirm the choice took effect, and SHALL indicate when the clock has not yet synced rather than showing a placeholder time.

#### Scenario: Section rendering

- **WHEN** the settings page loads
- **THEN** the sections above SHALL each be present as a headed block on the page, and each SHALL be reachable by scrolling without navigating away

#### Scenario: Stored zone is reflected in the dropdown

- **WHEN** the settings page loads and the stored timezone matches a listed zone
- **THEN** that zone SHALL be pre-selected and the custom field SHALL be hidden

#### Scenario: Unlisted zone falls back to custom

- **WHEN** the stored timezone does not match any listed zone
- **THEN** the **Custom** entry SHALL be selected and the free-text field SHALL be populated with the stored string

#### Scenario: Clock not yet synced

- **WHEN** the settings page loads before NTP has synced
- **THEN** the Timezone section SHALL indicate that it is waiting for NTP rather than rendering a placeholder time

#### Scenario: Saving does not warn about a restart

- **WHEN** the operator saves a timezone
- **THEN** the page SHALL confirm the change and refresh the displayed device time, without telling the user the device is restarting
