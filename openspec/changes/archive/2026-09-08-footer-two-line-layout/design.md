## Context

The current e-paper display footer uses a single line at y=182 for device name (left), clock (right), and setpoint with control symbol centered below. The display spec already supports date formatting via `Support::formatLocalDate()` producing `YYYY-MM-DD` and time formatting via `Support::formatLocalHhMm()` producing `HH:MM`. The partial refresh window spans y=30 to y=189 (160px height), which is sufficient for a two-line footer.

## Goals / Non-Goals

**Goals:**
- Maintain live clock behavior (footer updates on every refresh)
- Keep all footer content within the existing partial refresh window
- Preserve the visual hierarchy of temperature control information
- Use existing formatting functions without modification

**Non-Goals:**
- Change the refresh policy logic
- Modify the partial refresh window dimensions
- Add new fonts or graphical primitives
- Change the control symbol geometry

## Decisions

**Footer Layout (Two Lines):**
- Line 1 (y=175): device name (left, x=6), setpoint with degree symbol (right, x≈194)
- Line 2 (y=188): date+time (left, x=6), control symbol (right, x≈188-194)
- **Rationale:** Maintains visual separation between device identification and timestamp while keeping control status visible. The 13px vertical spacing between lines is consistent with typical text line height for the 9pt font.
- **Alternatives considered:** Single line with abbreviated date; three lines; moving device name to the value block. Two lines was chosen as the simplest improvement that fits within the existing refresh window.

**Right-Side Alignment (Right-Aligned):**
- Setpoint and control symbol are right-aligned at x≈194 rather than centered at x=100
- **Rationale:** Creates a cleaner right margin and separates the control information from the device/time information. Matches the existing right-alignment of the clock.
- **Alternatives considered:** Keeping centered alignment; left-aligning everything. Right-alignment provides better visual balance with the two-line left content.

**Control Symbol Vertical Position (y=190):**
- Symbol center at y=190, 2px below line 2 baseline (y=188)
- **Rationale:** Maintains visual association with line 2 while providing slight vertical separation. Fits within panel bounds (symbol extends y=184-196).
- **Alternatives considered:** Symbol at line 2 baseline (y=188); symbol at y=197 (current offset). y=190 balances visual alignment with physical constraints.

**Date+Time Format:**
- Format: `YYYY-MM-DD HH:MM` (16 characters)
- **Rationale:** ISO 8601 date format is unambiguous and sortable. Combines existing `formatLocalDate()` and `formatLocalHhMm()` functions.
- **Alternatives considered:** `YYYY/MM/DD HH:MM`; `DD-MM-YYYY HH:MM`; omitting date. Full ISO format provides maximum clarity.

**API Semantics:**
- `footerRight` parameter now carries date+time string instead of just time
- **Rationale:** Minimal API change; the semantic shift (clock → datetime) is contained within DisplayManager. No new parameters needed.
- **Alternatives considered:** Adding new parameters; creating a FooterContent struct. Keeping the existing parameter count reduces churn.

## Risks / Trade-offs

**[Horizontal space]** Date+time at x=6 extends to ~x=102, with right content starting at ~x=182. → 80px gap is acceptable per requirements; left side text wraps naturally within FreeSans9pt7b.

**[Vertical fit]** Control symbol at y=190 (radius 6) extends to y=196. → Within panel height of 200px; 4px margin at bottom is sufficient.

**[Buffer size]** Clock buffer grows from 8 to 17 bytes. → Negligible memory impact on ESP32-S2 (320KB internal SRAM).

**[Backward compatibility]** Existing code passing clock strings will now receive datetime. → Change is internal to display subsystem; no external API affected.
