## ADDED Requirements

### Requirement: Refresh power transient and brownout diagnosability

The firmware SHALL keep a display-induced supply sag diagnosable from the boot log alone, without external instrumentation.

The panel's charge pump draws a peak of roughly 25 mA for the duration of a
refresh, on the same 3.3 V rail as the WiFi radio. A brownout resets the chip
instantly and leaves no backtrace, so the boot-time `Reset reason:` line is the
only available signal, and it SHALL continue to be emitted on every boot.

`docs/EINK_DISPLAY_WIRING.md` SHALL state definitively whether bulk decoupling
across the module's 3V3/GND is required, together with the observation the
conclusion rests on and the conditions it was measured under. It SHALL NOT leave
the recommendation as an unevidenced precaution.

Where a supply sag is observed, firmware mitigations — widening the minimum
refresh interval, or suppressing refreshes during radio transmission — SHALL be
evaluated before a hardware change is mandated, because firmware reaches
existing units over OTA and a capacitor does not.

#### Scenario: Brownout is attributable after the fact

- **WHEN** the 3.3 V rail sags below the brownout threshold during a panel refresh and the chip resets
- **THEN** the next boot SHALL print a `Reset reason:` line identifying `BROWNOUT`, so the cause is recoverable from the log alone

#### Scenario: Decoupling guidance is evidence-backed

- **WHEN** a reader consults the wiring documentation to decide whether to fit the bulk capacitor
- **THEN** the guidance SHALL state a conclusion and the observation supporting it, rather than a precautionary recommendation

#### Scenario: Firmware mitigation is considered first

- **WHEN** brownout resets are observed and correlated with display refreshes
- **THEN** the available firmware mitigations SHALL be evaluated and their outcome recorded before bulk decoupling is documented as required
