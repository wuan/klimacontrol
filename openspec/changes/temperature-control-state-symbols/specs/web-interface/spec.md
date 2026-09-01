# web-interface Specification Delta

## MODIFIED Requirements

### Requirement: Symbol-based control state display

The dashboard SHALL display the temperature control state using visual symbols instead of text. The control bar SHALL show one of three symbols indicating the control state: a minus sign (`\u2212`) for **Inactive** (control disabled), a hollow circle (`\u25CB`) for **Active Off** (control enabled but output is zero), or a filled circle (`\u25CF`) for **Active On** (control enabled and output is non-zero). The symbols SHALL be styled with CSS for visibility and color-coded: red for inactive, purple for active off, green for active on.

#### Scenario: Inactive symbol displayed when control disabled

- **WHEN** `control_enabled` is `false`
- **THEN** the dashboard SHALL display the minus sign symbol (`\u2212`) in red

#### Scenario: Active Off symbol displayed at setpoint

- **WHEN** `control_enabled` is `true` and `control_active` is `false`
- **THEN** the dashboard SHALL display the hollow circle symbol (`\u25CB`) in purple

#### Scenario: Active On symbol displayed when heating/cooling

- **WHEN** `control_enabled` is `true` and `control_active` is `true`
- **THEN** the dashboard SHALL display the filled circle symbol (`\u25CF`) in green

#### Scenario: API fallback for old firmware

- **WHEN** the `/api/status` response does not include `control_active`
- **AND** `control_enabled` is `true`
- **THEN** the dashboard SHALL display the hollow circle symbol (`\u25CB`) assuming active off state

#### Scenario: API fallback when field missing and disabled

- **WHEN** the `/api/status` response does not include `control_active`
- **AND** `control_enabled` is `false`
- **THEN** the dashboard SHALL display the minus sign symbol (`\u2212`)
