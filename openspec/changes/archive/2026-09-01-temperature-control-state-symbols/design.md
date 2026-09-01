# Design: Temperature Control State Symbols

## Overview

This document describes the technical design for implementing symbol-based temperature control state indicators across web and e-paper displays.

## Architecture

### Data Flow

```
┌─────────────────────────────────────────────────────────────────┐
│                        DATA FLOW DIAGRAM                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────┐                                               │
│  │ SensorController │                                               │
│  │  ┌─────────────┐ │                                               │
│  │  │updateControl()│────update────┖                              │
│  │  │ - calculates │               │                              │
│  │  │   output     │               ▼                              │
│  │  │ - stores in  │         ┌─────────────┐                      │
│  │  │   lastControl│         │ lastControl │                      │
│  │  │   Output     │         │ Output      │                      │
│  │  └─────────────┘ │         └──────┬──────┘                      │
│  │                  │                │                              │
│  │  isControlActive()│◄─────────────┘                              │
│  │  returns output > 0│                                               │
│  └──────────┬────────┘                                               │
│              │                                                        │
│              ▼                                                        │
│  ┌─────────────────┐                                               │
│  │  /api/status    │                                               │
│  │  GET endpoint   │                                               │
│  │                 │                                               │
│  │  Returns JSON:  │                                               │
│  │  {             │                                               │
│  │    "control_  │                                               │
│  │     enabled": │                                               │
│  │     true,     │                                               │
│  │    "control_  │                                               │
│  │     active":  │                                               │
│  │     true      │                                               │
│  │  }             │                                               │
│  └──────────┬────────┘                                               │
│              │                                                        │
│     ┌────────┴────────┐                                             │
│     ▼                 ▼                                             │
│  ┌─────────┐    ┌─────────────┐                                  │
│  │  Web    │    │ E-Paper      │                                  │
│  │  UI     │    │ Display      │                                  │
│  │         │    │              │                                  │
│  │  • Symbol:     │  • Symbol:   │                                  │
│  │    Unicode     │    Drawn     │                                  │
│  │    ○ / ● / −  │    circle/   │                                  │
│  │                 │    line      │                                  │
│  │  • CSS colors  │  • Monochrome│                                  │
│  │                 │    (black)   │                                  │
│  │  • Fallback    │  • Setpoint  │                                  │
│  │    for old API │    in footer │                                  │
│  └─────────┘    └─────────────┘                                  │
│                                                                      │
└─────────────────────────────────────────────────────────────────┘
```

## Component Changes

### 1. SensorController

**File:** `src/SensorController.h`

```cpp
// Add member variable
private:
    float lastControlOutput = 0.0f;

// Add public method
public:
    bool isControlActive() const { return lastControlOutput > 0.0f; }
```

**File:** `src/SensorController.cpp`

```cpp
// In constructor, initialize:
SensorController::SensorController(Config::ConfigManager &config, [[maybe_unused]] StatusLed *statusLed)
    : config(config), lastReadingTimestamp(0), dataValid(false),
# ifdef ARDUINO
      dataMutex(xSemaphoreCreateMutex()),
      statusLed(statusLed),
# endif
      lastReadingTime(0),
      lastControlOutput(0.0f)  // NEW: Initialize to 0
{
    // ... existing code ...
}

// In updateControl(), store the output:
float SensorController::updateControl() {
    // ... existing calculation ...
    float output = proportional + integral + derivative;
    output = std::max(MinOutput, std::min(MaxOutput, output));
    
    lastControlOutput = output;  // NEW: Store for later query
    return output;
}
```

**Rationale:** Storing `lastControlOutput` allows querying the control state between updates without recalculating. Initializing to 0.0f ensures `isControlActive()` returns false at startup.

### 2. API Endpoint

**File:** `src/routes/StatusRoutes.cpp`

```cpp
// In GET /api/status handler, add:
doc["control_active"] = sensorController.isControlActive();
```

**Location:** After line 53 (`doc["control_enabled"] = sensorController.isControlEnabled();`)

**Rationale:** Exposes the control active state as a boolean, allowing clients to distinguish between active off and active on states.

### 3. Web Interface

**File:** `data/control.html`

Add CSS classes:
```css
/* Add to common.css or inline in control.html */
.control-symbol {
    font-size: 20px;
    font-weight: bold;
    display: inline-block;
    min-width: 24px;
    text-align: center;
}

.control-symbol.inactive { color: #dc3545; }  /* red */
.control-symbol.active-off { color: #667eea; } /* purple */
.control-symbol.active-on { color: #28a745; }   /* green */
```

Update JavaScript in control.html:
```javascript
// Helper functions for control symbol
function getControlSymbol(enabled, active) {
    if (enabled === undefined) return '-';
    if (active === undefined) return enabled ? '\u25CB' : '\u2212';
    if (!enabled) return '\u2212';
    if (active) return '\u25CF';
    return '\u25CB';
}

function getControlSymbolClass(enabled, active) {
    if (enabled === undefined) return 'inactive';
    if (active === undefined) return enabled ? 'active-off' : 'inactive';
    if (!enabled) return 'inactive';
    if (active) return 'active-on';
    return 'active-off';
}

// In updateStatus() function:
const symbol = getControlSymbol(currentStatus.control_enabled, currentStatus.control_active);
const symbolClass = getControlSymbolClass(currentStatus.control_enabled, currentStatus.control_active);

const controlStatusElement = document.getElementById('controlStatus');
if (controlStatusElement) {
    controlStatusElement.textContent = symbol;
    controlStatusElement.className = 'control-symbol ' + symbolClass;
}
```

**File:** `src/generated/control_gz.h`
- Will need to be regenerated after control.html changes via `scripts/compress_web.py`

**Rationale:** Unicode symbols provide clear visual distinction. Fallback logic ensures compatibility with old API versions that don't include `control_active`; that path reports "enabled, output unknown" (hollow circle, `active-off`) rather than claiming the device is actively heating. CSS classes provide color coding matching the existing scheme.

### 4. E-Paper Display

**File:** `src/display/RefreshPolicy.h`

`ControlState` lives here, not in `EPaperDisplay.h`: the refresh policy needs it
as an input, and `RefreshPolicy` is the deliberately Arduino-free half of the
display code so it stays testable in the `native` environment.

```cpp
enum class ControlState {
    INACTIVE,    // Control disabled
    ACTIVE_OFF,  // Control enabled, output = 0
    ACTIVE_ON    // Control enabled, output > 0
};
```

`evaluate()` gains the setpoint and the control state as trailing defaulted
parameters, and treats a change in either as a change worth showing. Both are
footer content the user can change from the web UI, so without this they would
only reach the panel when a measured value crossed hysteresis or the wall-clock
minute rolled over — and the minute never rolls over while NTP is unsynced.
Setpoint comparison uses a 0.05 K threshold, half the rendered precision, and
treats `NAN` (unknown) as its own state.

**File:** `src/display/EPaperDisplay.h`

```cpp
void render(const char *tempStr, const char *humStr,
            const char *footerName, const char *footerDateTime,
            ControlState controlState, const char *setpointStr,
            RefreshKind kind);
```

**File:** `src/display/EPaperDisplay.cpp`

Two-line, two-column footer. Everything is flush left at `FOOTER_MARGIN_X` or
flush right at `FOOTER_RIGHT_X`, so neither column drifts as its content changes
width:

```
--------------------------------------------  <- FOOTER_RULE_Y  = 152
klimacontrol                       22.0 (o)   <- FOOTER_LINE1_Y = 170
2026-09-01 12:34                        (*)   <- FOOTER_LINE2_Y = 192
```

Key points of the implementation:

- Baselines are 22 px apart, matching `FreeSans9pt7b`'s `yAdvance`. Ink runs 12 px
  above the baseline with 4 px descenders, so consecutive lines clear by 6 px.
- The **right column is drawn first**. It is the field that must never be
  truncated, and its extents fix how much room the left column has.
- The left column is truncated **per line** by `fitToWidth()`, against the width
  that row actually leaves free (`setpointLeftX` for line 1, `symbolLeftX` for
  line 2). `setTextWrap(false)` does not clip, it just keeps drawing, so an
  untruncated 31-character `device_name` would paint straight over the setpoint.
  The cut is marked with a trailing `.` — `…` is not available, the GFX free
  fonts only carry glyphs 0x20-0x7E.
- `getTextBounds()` returns `x1` as the **absolute** left edge of the ink for the
  cursor passed in, so ink spans `[cursor + x1, cursor + x1 + w]`. Every edge is
  derived from that identity; getting the sign of `x1` wrong is what pushed the
  degree ring into the last digit in an earlier revision.
- `REFRESH_WINDOW_H` is 170 rather than 160, i.e. down to the bottom edge of the
  panel, so both footer lines are inside the partial-refresh window. A field left
  outside it freezes between full refreshes.
- The degree ring sits at the digits' cap height (`FOOTER_LINE1_Y - 10`).
  Centring it on the digits makes it read as a lowercase `o`.
- All placement constants are named and derived (`PANEL_W / 2`, offsets from the
  line baselines) rather than literal, so the columns cannot silently
  desynchronise if the geometry changes.

**File:** `src/display/DisplayManager.h`

`formatClock()` becomes `formatDateTime()`, rendering `"YYYY-MM-DD HH:MM"` from
`Support::formatLocalDate()` + `Support::formatLocalHhMm()`. Both leave their
buffer empty for epoch 0, and the pair is emitted only if both succeed — a bare
time with no date would be the misleading half.

**File:** `src/display/DisplayManager.cpp`

Update update() method:
```cpp
void DisplayManager::update() {
    // ... existing code ...

    if (kind != RefreshKind::None) {
        const bool available = snapshot.valid && !std::isnan(temperature) && !std::isnan(humidity);

        char tempStr[16];
        char humStr[16];
        formatTemperature(tempStr, sizeof(tempStr), temperature, available);
        formatHumidity(humStr, sizeof(humStr), humidity, available);

        char clock[8];
        formatClock(clock, sizeof(clock));

        // NEW: Get control state and setpoint
        ControlState controlState;
        if (!sensorController.isControlEnabled()) {
            controlState = ControlState::INACTIVE;
        } else if (sensorController.isControlActive()) {
            controlState = ControlState::ACTIVE_ON;
        } else {
            controlState = ControlState::ACTIVE_OFF;
        }

        char setpointStr[8];
        float target = sensorController.getTargetTemperature();
        if (std::isnan(target)) {
            snprintf(setpointStr, sizeof(setpointStr), "--");
        } else {
            snprintf(setpointStr, sizeof(setpointStr), "%.1f", static_cast<double>(target));
        }

        panel.render(tempStr, humStr, deviceName, clock, controlState, setpointStr, kind);
    }
}
```

**File:** `src/display/RefreshPolicy.cpp`

No changes needed - formatting functions remain the same.

**Rationale:** Using GFX primitives allows drawing circles and lines directly on the e-paper display. FreeSans9pt7b provides a good balance between readability and size. The setpoint is centered in the footer with the control symbol to its left and degree symbol to its right.

## Error Handling

1. **NaN setpoint**: Display "--" for setpoint if target temperature is NaN
2. **Invalid sensor data**: Control remains inactive (output = 0) when data is invalid
3. **Missing API field**: Web interface falls back to text display for old API clients
4. **Display fault**: Existing fault handling in DisplayManager continues to work

## Testing Strategy

1. **Unit tests**: Verify `isControlActive()` returns correct values
2. **API tests**: Verify `/api/status` returns `control_active` field
3. **Web UI tests**: Verify symbols display correctly in all three states
4. **E-paper tests**: Verify symbols are drawn correctly and setpoint is visible
5. **Backwards compatibility**: Verify old API clients still work

## Migration Path

1. Deploy firmware with new API field
2. Web interface automatically uses symbols when `control_active` is available
3. Old web clients continue to work, showing text or symbol based on API response
4. No database/configuration migration needed
