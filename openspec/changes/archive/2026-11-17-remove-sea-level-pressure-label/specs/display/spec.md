# Spec Delta — display

## MODIFIED Requirements

### Requirement: Display field labels

Each configured value SHALL be rendered with a fixed label where it has one. Humidity SHALL use `%rH`; dew point SHALL use `DP` with a drawn degree ring; pressure SHALL use `P`; sea-level pressure SHALL carry no label and SHALL be identified by its `hPa` unit alone; CO₂ SHALL use `CO2`; illuminance SHALL use `lux`; VOC index SHALL use `VOC`; PM2.5 SHALL use `PM2.5`. Units SHALL be `hPa` for pressure values, `ppm` for CO₂, `lux` for illuminance and `ug/m3` for PM2.5.

#### Scenario: Humidity line

- **WHEN** humidity is 47 %RH
- **THEN** the line SHALL render `47 %rH`

#### Scenario: Dew point line

- **WHEN** dew point is 12.4 °C
- **THEN** the line SHALL render `DP 12.4` followed by the drawn degree ring

#### Scenario: Pressure and sea-level pressure lines

- **WHEN** pressure is 1013 hPa and sea-level pressure is 1016 hPa
- **THEN** the pressure line SHALL render `P 1013 hPa` and the sea-level line SHALL render `1016 hPa` without a label prefix

#### Scenario: Sea-level pressure placeholder

- **WHEN** sea-level pressure is selected and its reading is unavailable
- **THEN** the line SHALL render `-- hPa` without a label prefix

#### Scenario: CO₂ line

- **WHEN** CO₂ concentration is 812 ppm
- **THEN** the line SHALL render `CO2 812 ppm`

#### Scenario: VOC index line

- **WHEN** the VOC index is 120
- **THEN** the line SHALL render `VOC 120`

#### Scenario: PM2.5 line uses the ASCII unit

- **WHEN** PM2.5 concentration is 8 µg/m³
- **THEN** the line SHALL render `PM2.5 8 ug/m3`

### Requirement: Warning icon layout

The e-paper value block SHALL include warning slots in both margins — the left margin and its mirror image in the right margin: a filled warning triangle with the panel's drawn-symbol technique and the active warning's label in the built-in 5x7 font directly below it. Both slots SHALL show the same token.

The warning triangle SHALL be 34 px tall and 34 px wide at its base, anchored to the temperature baseline: its apex SHALL sit 34 px above that baseline, its base SHALL coincide with it, and the label's glyph top SHALL sit 6 px below it. In the two-line layout the triangle SHALL occupy y 47..81 with its label at y=87; in the three-line layout it SHALL occupy y 39..73 with its label at y=79. The slot centres SHALL be x=31 and the mirror image at x=169.

Each slot SHALL lie entirely inside the partial-refresh window. The temperature, all configured value lines and the footer SHALL keep their geometry and position whether or not the warning slots are occupied — the warning is purely additive, and showing or clearing it SHALL NOT shift or rescale any other element. The warning label SHALL sit above the second value line so a long second value cannot collide with it.

#### Scenario: Warning shown with a long second line

- **WHEN** a warning is active and the second line is a wide value such as `PM2.5 8 ug/m3`
- **THEN** the warning triangle and label SHALL remain in both margins above the second line, and the second line SHALL not be shifted or overwritten

#### Scenario: Warning follows the temperature in the three-line layout

- **WHEN** the three-line layout is rendered
- **THEN** the warning triangle SHALL move up with the temperature to y 39..73 and its label to y=79

#### Scenario: Warning shown beside unchanged values

- **WHEN** a warning becomes active while the readings are stable
- **THEN** the panel SHALL show the triangle and label in both margins, and the temperature, configured values and footer SHALL remain in their existing positions

#### Scenario: Warning cleared restores the empty margins

- **WHEN** the active warning clears
- **THEN** both margins SHALL be blank again and no other element SHALL have moved

#### Scenario: Warning refresh is partial, not full

- **WHEN** a warning appears or clears and no other displayed value has changed
- **THEN** the repaint SHALL be a partial refresh inside the existing window, and SHALL NOT introduce a full-panel refresh
