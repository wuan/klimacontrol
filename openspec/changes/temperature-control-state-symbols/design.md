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
    if (active === undefined) return enabled ? 'active-on' : 'inactive';
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

**Rationale:** Unicode symbols provide clear visual distinction. Fallback logic ensures compatibility with old API versions that don't include `control_active`. CSS classes provide color coding matching the existing scheme.

### 4. E-Paper Display

**File:** `src/display/EPaperDisplay.h`

Add enum for control state:
```cpp
enum class ControlState {
    INACTIVE,    // Control disabled
    ACTIVE_OFF,   // Control enabled, output = 0
    ACTIVE_ON     // Control enabled, output > 0
};
```

Update render signature:
```cpp
void render(const char *tempStr, const char *humStr,
            const char *footerLeft, const char *footerRight,
            ControlState controlState, const char *setpointStr,
            RefreshKind kind);
```

**File:** `src/display/EPaperDisplay.cpp`

Add header include:
```cpp
#include <Fonts/FreeSans9pt7b.h>
```

Add helper function:
```cpp
namespace {
    void drawControlSymbol(int16_t x, int16_t y, ControlState state) {
        switch (state) {
            case ControlState::INACTIVE:
                display.drawFastHLine(x - 5, y, 10, GxEPD_BLACK);
                break;
            case ControlState::ACTIVE_OFF:
                display.drawCircle(x, y, 6, GxEPD_BLACK);
                break;
            case ControlState::ACTIVE_ON:
                display.fillCircle(x, y, 6, GxEPD_BLACK);
                break;
        }
    }
}
```

Update runPagedDraw:
```cpp
void EPaperDisplay::runPagedDraw(const char *tempStr, const char *humStr,
                                 const char *footerLeft, const char *footerRight,
                                 ControlState controlState, const char *setpointStr) {
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // ... existing temperature/humidity drawing ...

        // Footer rule
        display.drawFastHLine(FOOTER_MARGIN_X, FOOTER_RULE_Y,
                              PANEL_W - 2 * FOOTER_MARGIN_X, GxEPD_BLACK);

        // Set 9pt font for setpoint
        display.setFont(&FreeSans9pt7b);

        // Measure setpoint text
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        display.getTextBounds(setpointStr, 0, FOOTER_TEXT_Y, &x1, &y1, &w, &h);

        // Center setpoint at x=100
        int16_t setpointX = 100 - (w + x1) / 2;

        // Draw control symbol 8px left of setpoint
        int16_t symbolX = setpointX + x1 - 8;
        drawControlSymbol(symbolX, 175, controlState);

        // Draw setpoint text
        display.setCursor(setpointX, FOOTER_TEXT_Y);
        display.print(setpointStr);

        // Draw degree circle 4px right of setpoint
        int16_t degreeX = setpointX + w - x1 + 4;
        display.drawCircle(degreeX, 175, 2, GxEPD_BLACK);

        // Reset to built-in font for footer left/right
        display.setFont(nullptr);

        // Draw device name and clock
        // ... existing code ...
    } while (display.nextPage());
}

void EPaperDisplay::render(const char *tempStr, const char *humStr,
                           const char *footerLeft, const char *footerRight,
                           ControlState controlState, const char *setpointStr,
                           RefreshKind kind) {
    // ... existing window setup ...
    runPagedDraw(tempStr, humStr, footerLeft, footerRight, controlState, setpointStr);
    // ... existing duration tracking ...
}
```

**File:** `src/display/DisplayManager.h`

No changes needed to header (already has render method).

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
