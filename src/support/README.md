# Support Classes

## SmoothBlend

The `SmoothBlend` class provides smooth color transitions for LED strips. It interpolates between initial colors and
target colors over a specified duration.

### Usage

#### Blend all LEDs to a single color

```cpp
#include "support/SmoothBlend.h"

// Create a blend to blue over 2 seconds (default duration)
Support::SmoothBlend blend(strip, 0x0000FF);

// In your loop, call step() repeatedly
while (blend.step()) {
    // The blend animates automatically
    // step() returns false when complete
}
```

#### Blend each LED to different colors

```cpp
#include "support/SmoothBlend.h"

// Set different target colors for each LED
std::vector<Strip::Color> targets;
for (int i = 0; i < strip.length(); i++) {
    targets.push_back(wheel(i * 255 / strip.length()));
}

// Create blend with custom duration (1 second)
Support::SmoothBlend blend(strip, targets, 1000);

// Animate
while (blend.step()) {
    delay(10); // Optional delay between steps
}
```

#### Check if blend is complete

```cpp
Support::SmoothBlend blend(strip, 0xFF0000, 500); // 500ms duration

if (blend.isComplete()) {
    // Blend finished
}
```

### Features

- **Linear interpolation**: Smooth color transitions using linear blending
- **Per-LED colors**: Each LED can have its own target color
- **Configurable duration**: Set blend duration in milliseconds (default: 2000ms)
- **Non-blocking**: Call `step()` in your loop for smooth animation
- **Platform independent**: Works on both Arduino and native platforms

### Implementation Notes

The class captures the initial colors from the strip when constructed, then smoothly blends to the target colors over
the specified duration. The blend progress is calculated using the `millis()` function for accurate timing.

The blend uses a linear interpolation formula:

```
blended_value = start_value * fade_progress + end_value * (1 - fade_progress)
```

Where `fade_progress` goes from 1.0 (at start) to 0.0 (at end).
