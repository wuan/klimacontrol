# Show Parameter Configuration Guide

## Overview

ledz now supports configurable parameters for shows. Parameters are passed as JSON objects and automatically parsed by the ShowFactory.

**✅ Web UI Implemented**: The control interface now includes parameter controls that automatically appear based on the selected show:
- **Solid Show**: Color picker for RGB selection
- **Mandelbrot Show**: Input fields for Cre0, Cim0, Cim1 (complex plane coordinates), scale, max_iterations, and color_scale
- **Chaos Show**: Input fields for Rmin, Rmax, and Rdelta (logistic map parameters)
- **ColorRanges Show with Blends**: Linear gradients between colors using `blends` parameter
- **ColorRanges Show**: Dynamic color inputs with flag presets (Ukraine 🇺🇦, Italy 🇮🇹), optional custom ranges

## Architecture

```
Web UI → API → ShowController → Queue → ShowFactory → Show Instance
         (JSON)                (JSON)    (parse)      (constructed)
```

### Flow:
1. User configures parameters in web UI
2. UI sends JSON to `/api/show` endpoint
3. ShowController queues the command (thread-safe)
4. LED task processes queue
5. ShowFactory parses JSON and creates show with parameters
6. Parameters are saved to NVS for persistence

## API Usage

### Change Show with Parameters

**Endpoint**: `POST /api/show`

**Request Body**:
```json
{
  "name": "Solid",
  "params": {
    "r": 255,
    "g": 0,
    "b": 0
  }
}
```

**Example: Set Solid to Red**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Solid","params":{"r":255,"g":0,"b":0}}'
```

**Example: Set Solid to Blue**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Solid","params":{"r":0,"g":0,"b":255}}'
```

**Example: Set Mandelbrot Parameters**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Mandelbrot","params":{"Cre0":-0.5,"Cim0":0,"Cim1":-0.5,"scale":5,"max_iterations":50,"color_scale":10}}'
```

**Example: Set Chaos Parameters**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Chaos","params":{"Rmin":3.5,"Rmax":4.0,"Rdelta":0.001}}'
```

**Example: Set ColorRanges with Linear Gradient (replaces TwoColorBlend)**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Solid","params":{"colors":[[255,0,0],[0,0,255]],"gradient":true}}'
```

**Example: Set ColorRanges Parameters (Ukraine Flag)**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"ColorRanges","params":{"colors":[[0,87,183],[255,215,0]]}}'
```

**Example: Set ColorRanges Parameters (Italian Flag)**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"ColorRanges","params":{"colors":[[0,140,69],[255,255,255],[205,33,42]]}}'
```

**Example: Set ColorRanges with Custom Ranges**
```bash
curl -X POST http://192.168.1.100/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"ColorRanges","params":{"colors":[[255,0,0],[255,255,255],[0,0,255]],"ranges":[25,75]}}'
```

## Supported Shows & Parameters

### Solid
**Parameters**:
- `r` (uint8_t, 0-255): Red component (default: 255)
- `g` (uint8_t, 0-255): Green component (default: 255)
- `b` (uint8_t, 0-255): Blue component (default: 255)

**Example JSON**:
```json
{"r": 255, "g": 100, "b": 0}  // Orange
{"r": 128, "g": 0, "b": 128}  // Purple
{"r": 0, "g": 255, "b": 0}    // Green
```

### Mandelbrot
**Parameters**:
- `Cre0` (float): Real component minimum (default: -1.05)
- `Cim0` (float): Imaginary component minimum (default: -0.3616)
- `Cim1` (float): Imaginary component maximum (default: -0.3156)
- `scale` (unsigned int): Scale factor for scanning (default: 5, range: 1-20)
- `max_iterations` (unsigned int): Maximum iterations for convergence test (default: 50, range: 10-200)
- `color_scale` (unsigned int): Color scaling factor (default: 10, range: 1-50)

**Example JSON**:
```json
{"Cre0": -0.5, "Cim0": 0, "Cim1": -0.5, "scale": 5, "max_iterations": 50, "color_scale": 10}  // Classic view
{"Cre0": -1.05, "Cim0": -0.3616, "Cim1": -0.3156, "scale": 5, "max_iterations": 50, "color_scale": 10}  // Default spiral
{"Cre0": -0.8, "Cim0": -0.2, "Cim1": 0.2, "scale": 10, "max_iterations": 100, "color_scale": 5}  // High detail view
```

### Chaos
**Parameters**:
- `Rmin` (float): Starting R value for logistic map (default: 2.95)
- `Rmax` (float): Maximum R value (default: 4.0)
- `Rdelta` (float): R increment per iteration (default: 0.0002)

**Example JSON**:
```json
{"Rmin": 2.95, "Rmax": 4.0, "Rdelta": 0.0002}  // Default - slow evolution
{"Rmin": 3.5, "Rmax": 4.0, "Rdelta": 0.001}    // Faster evolution, starting in chaotic region
{"Rmin": 2.8, "Rmax": 3.6, "Rdelta": 0.0001}   // Slower, exploring period-doubling
```

### ColorRanges (Solid)
**Parameters**:
- `colors` (array of RGB arrays, required): List of colors as `[r, g, b]` arrays. Minimum 1 color.
- `ranges` (array of floats, optional): Boundary percentages (0-100) where colors transition. **Must have exactly N-1 values for N colors.** If omitted, colors distribute equally. Note: ignored in gradient mode.
- `gradient` (boolean, optional): If `true`, colors act as waypoints with smooth interpolation. If `false` (default), colors fill sections with sharp boundaries.

**Description**: Displays color sections across the LED strip. Perfect for flags, banners, multi-color patterns, and gradients. Colors are smoothly blended from the current state using SmoothBlend.

**Note**: This show replaces the former `TwoColorBlend` show. To create a gradient, use `gradient: true`.

**Range Calculation**:
- **Equal Distribution** (default): If `ranges` is omitted, colors are distributed equally across the strip
  - 2 colors: 50% each (no ranges needed)
  - 3 colors: 33.3% each (no ranges needed)
  - 4 colors: 25% each (no ranges needed)
- **Custom Distribution**: Provide boundary percentages. **For N colors, you need N-1 boundary values.**
  - 2 colors: 1 boundary (e.g., `[60]` = 60% color1, 40% color2)
  - 3 colors: 2 boundaries (e.g., `[25, 75]` = 25% color1, 50% color2, 25% color3)
  - 4 colors: 3 boundaries (e.g., `[20, 50, 80]` = 20% color1, 30% color2, 30% color3, 20% color4)

**Modes**:
- **Solid mode** (default, `gradient: false`): Colors fill sections with sharp boundaries
- **Gradient mode** (`gradient: true`): Colors act as waypoints, smooth interpolation between them

In gradient mode, colors are treated as evenly-spaced waypoints across the strip:
- 2 colors: waypoints at 0% and 100% → full strip gradient
- 3 colors: waypoints at 0%, 50%, 100% → gradient A→B in first half, B→C in second half
- N colors: waypoints evenly distributed from 0% to 100%

**Example JSON**:
```json
// Single warm white color
{
  "colors": [[255, 250, 230]]
}

// Ukraine Flag - Blue and Yellow, sharp boundary (50% each)
{
  "colors": [[0, 87, 183], [255, 215, 0]]
}

// Italian Flag - Green, White, Red with sharp boundaries
{
  "colors": [[0, 140, 69], [255, 255, 255], [205, 33, 42]]
}

// Linear gradient from red to blue (replaces TwoColorBlend)
// Gradient spans the ENTIRE strip
{
  "colors": [[255, 0, 0], [0, 0, 255]],
  "gradient": true
}

// Rainbow gradient (smooth transitions between all colors)
// Colors are waypoints: red at 0%, orange at 20%, yellow at 40%, etc.
{
  "colors": [
    [255, 0, 0],     // Red (0%)
    [255, 127, 0],   // Orange (20%)
    [255, 255, 0],   // Yellow (40%)
    [0, 255, 0],     // Green (60%)
    [0, 0, 255],     // Blue (80%)
    [75, 0, 130]     // Indigo (100%)
  ],
  "gradient": true
}

// 2 colors with custom boundary: 60% red, 40% blue (solid mode)
{
  "colors": [[255, 0, 0], [0, 0, 255]],
  "ranges": [60]
}

// 3 colors with custom ranges: 25% red, 50% white, 25% blue (solid mode)
{
  "colors": [[255, 0, 0], [255, 255, 255], [0, 0, 255]],
  "ranges": [25, 75]
}
```

**Flag Presets**:
- **🇺🇦 Ukraine**: Blue (#0057B7) and Yellow (#FFD700)
- **🇮🇹 Italy**: Green (#008C45), White (#FFFFFF), Red (#CD212A)

### Other Shows
Rainbow, ColorRun, Jump currently don't support parameters and will use their default behavior.

## Adding Parameter Support to New Shows

### Step 1: Define Parameters in Show Class

```cpp
// show/MyShow.h
class MyShow : public Show {
private:
    int speed;
    Color color;
public:
    MyShow(int speed = 50, Color color = color(255,255,255));
    // ...
};
```

### Step 2: Add JSON Parsing in ShowFactory

```cpp
// ShowFactory.cpp in createShow(name, paramsJson):
else if (strcmp(name, "MyShow") == 0) {
    // Parse parameters with defaults
    int speed = doc["speed"] | 50;  // Default to 50
    uint8_t r = doc["r"] | 255;
    uint8_t g = doc["g"] | 255;
    uint8_t b = doc["b"] | 255;

    Serial.printf("ShowFactory: Creating MyShow speed=%d RGB(%d,%d,%d)\n",
                  speed, r, g, b);
    return new Show::MyShow(speed, color(r, g, b));
}

// Example: Mandelbrot with all parameters
else if (strcmp(name, "Mandelbrot") == 0) {
    float Cre0 = doc["Cre0"] | -1.05f;
    float Cim0 = doc["Cim0"] | -0.3616f;
    float Cim1 = doc["Cim1"] | -0.3156f;
    unsigned int scale = doc["scale"] | 5;
    unsigned int max_iterations = doc["max_iterations"] | 50;
    unsigned int color_scale = doc["color_scale"] | 10;

    return new Show::Mandelbrot(Cre0, Cim0, Cim1, scale, max_iterations, color_scale);
}
```

### Step 3: Test via API

```bash
curl -X POST http://device-ip/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"MyShow","params":{"speed":100,"r":255,"g":0,"b":0}}'
```

## Adding Web UI for Parameters

### Simple Example: Color Picker for Solid

Add this to the control HTML page after the show selector:

```html
<div class="control-group" id="solidColorPicker" style="display:none;">
    <label class="control-label">Color</label>
    <input type="color" id="colorInput" value="#ffffff">
    <button onclick="updateSolidColor()">Apply Color</button>
</div>

<script>
// Show/hide color picker based on selected show
document.getElementById('showSelect').addEventListener('change', (e) => {
    const picker = document.getElementById('solidColorPicker');
    picker.style.display = (e.target.value === 'Solid') ? 'block' : 'none';
});

async function updateSolidColor() {
    const hex = document.getElementById('colorInput').value;
    // Convert hex to RGB
    const r = parseInt(hex.substr(1,2), 16);
    const g = parseInt(hex.substr(3,2), 16);
    const b = parseInt(hex.substr(5,2), 16);

    try {
        await fetch('/api/show', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                name: 'Solid',
                params: { r, g, b }
            })
        });
    } catch (error) {
        console.error('Failed to update color:', error);
    }
}
</script>
```

### Advanced Example: Dynamic Parameter Forms

```javascript
// Show parameter schemas
const showParams = {
    'Solid': [
        { name: 'r', label: 'Red', type: 'range', min: 0, max: 255, default: 255 },
        { name: 'g', label: 'Green', type: 'range', min: 0, max: 255, default: 255 },
        { name: 'b', label: 'Blue', type: 'range', min: 0, max: 255, default: 255 }
    ],
    'Mandelbrot': [
        { name: 'Cre0', label: 'Cre0 (Real min)', type: 'number', step: 0.01, default: -1.05 },
        { name: 'Cim0', label: 'Cim0 (Imaginary min)', type: 'number', step: 0.01, default: -0.3616 },
        { name: 'Cim1', label: 'Cim1 (Imaginary max)', type: 'number', step: 0.01, default: -0.3156 },
        { name: 'scale', label: 'Scale', type: 'number', min: 1, max: 20, default: 5 },
        { name: 'max_iterations', label: 'Max Iterations', type: 'number', min: 10, max: 200, default: 50 },
        { name: 'color_scale', label: 'Color Scale', type: 'number', min: 1, max: 50, default: 10 }
    ]
};

// Generate parameter form dynamically
function showParameterForm(showName) {
    const container = document.getElementById('parameterForm');
    container.innerHTML = '';

    const params = showParams[showName];
    if (!params) return;

    params.forEach(param => {
        const input = document.createElement('input');
        input.type = param.type;
        input.id = `param_${param.name}`;
        input.min = param.min;
        input.max = param.max;
        input.step = param.step;
        input.value = param.default;

        const label = document.createElement('label');
        label.textContent = param.label;

        container.appendChild(label);
        container.appendChild(input);
    });
}

// Apply parameters
async function applyShowParameters(showName) {
    const params = {};
    const paramDefs = showParams[showName];

    paramDefs.forEach(param => {
        const input = document.getElementById(`param_${param.name}`);
        params[param.name] = param.type === 'range' ?
            parseInt(input.value) : parseFloat(input.value);
    });

    await fetch('/api/show', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: showName, params })
    });
}
```

## Persistence

Parameters are automatically saved to NVS when a show is changed. On restart:
1. ShowController loads `ShowConfig.params_json`
2. Passes parameters to ShowFactory
3. Show is recreated with saved parameters

## Memory Considerations

- **ShowCommand**: 256 bytes for `params_json` (stored in queue)
- **ShowConfig**: 256 bytes for `params_json` (stored in NVS)
- **Stack usage**: ~512 bytes during JSON parsing (StaticJsonDocument<512>)

Total overhead: ~1KB RAM when queue is full (5 commands × 256 bytes)

## Testing

### Test Parameter Persistence

```bash
# 1. Set Solid to red
curl -X POST http://device-ip/api/show \
  -d '{"name":"Solid","params":{"r":255,"g":0,"b":0}}'

# 2. Restart device
curl -X POST http://device-ip/api/restart

# 3. Device should boot with red Solid show
```

### Test Parameter Validation

```bash
# Invalid JSON - should use defaults
curl -X POST http://device-ip/api/show \
  -d '{"name":"Solid","params":{"invalid"}}'

# Missing parameters - should use defaults
curl -X POST http://device-ip/api/show \
  -d '{"name":"Solid","params":{}}'

# Partial parameters - should use defaults for missing
curl -X POST http://device-ip/api/show \
  -d '{"name":"Solid","params":{"r":255}}'  # g=255, b=255 (white→red blend)

# Mandelbrot with partial parameters
curl -X POST http://device-ip/api/show \
  -d '{"name":"Mandelbrot","params":{"Cre0":-0.8,"max_iterations":100}}'  # Other params use defaults
```

## Common Color Presets for Solid Show

```javascript
const colorPresets = {
    'Red': {r:255, g:0, b:0},
    'Green': {r:0, g:255, b:0},
    'Blue': {r:0, g:0, b:255},
    'White': {r:255, g:255, b:255},
    'Warm White': {r:255, g:180, b:107},
    'Orange': {r:255, g:100, b:0},
    'Yellow': {r:255, g:255, b:0},
    'Purple': {r:128, g:0, b:128},
    'Pink': {r:255, g:192, b:203},
    'Cyan': {r:0, g:255, b:255},
    'Magenta': {r:255, g:0, b:255}
};

// Apply preset
async function applyPreset(name) {
    const color = colorPresets[name];
    await fetch('/api/show', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: 'Solid', params: color })
    });
}
```

## Debug Output

The ShowFactory and ShowController log parameter information to Serial:

```
ShowFactory: Creating Solid with color RGB(255,0,0)
ShowController: Switched to show: Solid with params: {"r":255,"g":0,"b":0}
```

Monitor serial output to verify parameters are being parsed correctly.

## Next Steps

1. ✅ ~~Add color picker UI to web interface~~ (Completed)
2. ✅ ~~Add parameter forms for Mandelbrot coordinates~~ (Completed)
3. Add preset buttons for common colors
4. Add parameter support to other shows (ColorRun speed, Rainbow rate, etc.)
5. Add "favorite" presets that users can save
6. Add parameter validation (range checking)
