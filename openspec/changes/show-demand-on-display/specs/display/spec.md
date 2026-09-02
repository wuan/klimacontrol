# display Specification Delta

## ADDED Requirements

### Requirement: Controller demand bar

The panel footer SHALL show the controller's demand as a bar of discrete segments on footer line 2, positioned between the date/time and the control symbol. The bar SHALL be drawn only while temperature control is enabled: when control is off the inactive symbol already conveys the state, and when control is on the bar SHALL be drawn even at zero demand, so that "enabled and calling for nothing" is distinguishable from "switched off".

A numeric percentage SHALL NOT be shown on the panel. The panel repaints whenever a displayed value changes, so a value that moves on every control tick would hold refreshes at the minimum-interval floor indefinitely, for a precision that is not meaningful on a plant with hours-long dynamics.

The bar SHALL be sized so that the date/time in the same row still fits, and the left column SHALL continue to truncate to the space the right column leaves.

#### Scenario: Bar shown while control is enabled

- **WHEN** control is enabled and the controller demand is roughly half of its range
- **THEN** the footer SHALL show about half the bar's segments filled

#### Scenario: Bar hidden while control is disabled

- **WHEN** control is disabled
- **THEN** no demand bar SHALL be drawn, and the inactive symbol SHALL be shown as before

#### Scenario: Zero demand while enabled

- **WHEN** control is enabled and demand is zero
- **THEN** an empty bar SHALL be drawn rather than no bar

#### Scenario: Date and time still fit

- **WHEN** the bar is drawn alongside a full date and time
- **THEN** the date/time SHALL remain legible, truncated to the room the right column leaves

### Requirement: Demand is quantised with hysteresis before display

Demand SHALL be reduced to a small number of buckets before it reaches the display or the refresh decision, and moving between buckets SHALL require clearing the boundary by a hysteresis margin. Quantisation alone is insufficient: a demand hovering on a boundary would change bucket on alternate ticks and repaint the panel as often as a raw value would.

The refresh policy SHALL treat a change of bucket as a change worth showing, subject to the existing minimum-interval floor, and SHALL compare buckets directly rather than applying a further threshold of its own.

#### Scenario: Bucket holds on a boundary

- **WHEN** the demand fraction sits exactly on a bucket boundary
- **THEN** the displayed bucket SHALL remain whichever it already was

#### Scenario: Dithering does not move the bar

- **WHEN** the demand fraction oscillates by less than the hysteresis margin either side of a boundary
- **THEN** the displayed bucket SHALL NOT change, and no refresh SHALL be triggered by it

#### Scenario: A decisive move changes the bucket

- **WHEN** the demand fraction clears a boundary by more than the hysteresis margin
- **THEN** the displayed bucket SHALL change

#### Scenario: Bucket change triggers a refresh

- **WHEN** the bucket changes and all other displayed values are unchanged
- **THEN** a refresh SHALL be scheduled

#### Scenario: Unchanged bucket triggers nothing

- **WHEN** the bucket is unchanged and no other displayed value has changed
- **THEN** no refresh SHALL be scheduled, even though the raw demand moved

#### Scenario: Bucket changes respect the interval floor

- **WHEN** the bucket changes sooner than the minimum interval allows
- **THEN** the refresh SHALL be deferred, as it is for a setpoint change

#### Scenario: Steady demand costs no refreshes

- **WHEN** the controller output has settled such that the bucket no longer moves
- **THEN** the panel SHALL return to its baseline refresh rate, driven only by the clock and by sensor readings
