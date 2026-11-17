# Design — add-configurable-display-fields

## Context

See `proposal.md` for motivation. The current pipeline is:

```text
DisplayManager::update()  (Network task, 1 s tick)
        -> SensorController::getSnapshot()   (single lock)
        -> RefreshPolicy::evaluate()
        -> EPaperDisplay::render()           (paged draw)
```

The existing display has a fixed two-line value block, a warning icon in each
margin, a two-line footer and a pure, native-testable refresh policy. The
sensor snapshot already carries every measurement this change exposes, so no
sensor driver or controller API changes are required.

Constraints that shape the approach:

- The `native` environment builds config, display policy and formatting code
  without Arduino/FreeRTOS; hardware drawing lives only under `#ifdef ARDUINO`.
- The partial-refresh window is `(0, 30)` to `(199, 199)`; every mutable value
  line and the footer must stay inside it.
- The header band (`y 0..29`) is outside the partial window and may only hold
  static content.
- GFX free fonts carry only printable ASCII; degree marks and units such as
  `µg/m³` must be drawn or transliterated.
- Configuration changes are saved then applied by restart, matching every other
  settings route. There is no live-reconfigure path.

## Goals / Non-Goals

**Goals:**

- Two configurable value slots (second and third) with stable, documented
  tokens, defaults and NVS persistence.
- Fixed two-line and three-line layouts chosen by configuration, not by the
  current sensor set, so a transient missing reading cannot make the panel jump.
- Per-slot availability and hysteresis in the refresh policy, including the
  pressure-only sensor case.
- Native-testable field metadata, formatting and refresh logic.
- Pixel-accurate host renderings for the new layouts before firmware work.

**Non-Goals:**

- Configurable primary temperature; arbitrary `Sensor::MeasurementType` values;
  user-editable labels, units or hysteresis.
- Runtime capability detection or automatic fallback between layouts.
- Changes to the footer, header band, warning vocabulary or AP/splash screens.
- New sensor drivers or dependencies.

## Decisions

### D1. Config model: a small display-specific enum, not raw measurement types

`Config::DisplayConfig` gains:

```text
second_field = DisplayField::Humidity   // always present
third_field  = DisplayField::None      // optional; None selects two-line layout
```

`DisplayField` is a stable `uint8_t` enum with explicit values:

| Value | Token | Measurement |
|---:|---|---|
| 0 | `humidity` | `RelativeHumidity` |
| 1 | `dew_point` | `DewPoint` |
| 2 | `pressure` | `Pressure` |
| 3 | `sea_level_pressure` | `SeaLevelPressure` |
| 4 | `co2` | `CO2` |
| 5 | `illuminance` | `Illuminance` |
| 6 | `voc_index` | `VocIndex` |
| 7 | `pm25` | `PM25Concentration` |
| 8 | `none` | no third line |

NVS keys are `disp_fld2` and `disp_fld3` (both within the 15-character limit and
checked with `Config::nvsKeyFits`). `validateDisplayConfig()` falls back to
`Humidity` for a corrupt/unknown second value and `None` for a corrupt/unknown
third value.

**Why not persist `Sensor::MeasurementType`:**
- It couples persisted config to an internal enum that also contains system and
  diagnostic values, and permits selections the panel cannot format.
- A curated enum gives a stable API token vocabulary, explicit labels, units and
  hysteresis per field.
- Adding a future field is a deliberate enum/spec extension, not an accidental
  consequence of the sensor layer growing.

**Why not infer the layout from connected sensors:** the display configuration is
saved and applied by restart; inferring from the live snapshot would make a
long second line jump if a sensor briefly disappears. Explicit configuration is
predictable and matches the project’s “save then restart” convention. A missing
reading stays a placeholder in the configured layout.

### D2. Field metadata and formatting live in a native-testable helper

A new `src/display/DisplayField.{h,cpp}` maps a `DisplayField` to a fixed
`FieldSpec`:

| Field | Rendered line | Decimals | Hysteresis | Availability note |
|---|---|---:|---:|---|
| humidity | `47 %rH` | 0 | 1.0 %RH | any humidity measurement |
| dew_point | `DP 12.4` + drawn degree ring | 1 | 0.1 °C | any dew point measurement |
| pressure | `P 1013 hPa` | 0 | 0.5 hPa | any pressure measurement |
| sea_level_pressure | `SLP 1016 hPa` | 0 | 0.5 hPa | pressure drivers emit it only when elevation > 0 |
| co2 | `CO2 812 ppm` | 0 | 10 ppm | `CO2` is `int32_t` |
| illuminance | `320 lux` | 0 | 5 lux | any illuminance measurement |
| voc_index | `VOC 120` | 0 | 5 | SGP40 emits it only with temperature and humidity available |
| pm25 | `PM2.5 8 ug/m3` | 0 | 1 µg/m³ | `PM25Concentration` |

The helper owns:
- mapping the display field to a `Sensor::MeasurementType`;
- reading a float or `int32_t` from the snapshot’s `std::variant`;
- formatting the full line, including prefix and ASCII unit;
- returning whether the line needs a drawn degree ring;
- exposing the field-specific hysteresis to the refresh policy.

Placeholders retain the label/unit and replace only the numeric part:
`P -- hPa`, `DP --` + ring, `CO2 -- ppm`, `PM2.5 -- ug/m3`, `-- %rH`.

**Why a helper rather than branches in `DisplayManager`:** the mapping and
formatting are pure C++ and can be unit-tested natively; `DisplayManager` stays
an Arduino-side orchestrator. The helper avoids duplicating the float/int32
variant handling at every call site.

### D3. Layout: fixed two-line and three-line geometries

The value block remains `(0, 30)` to `(199, 139)`. The two layouts are:

| Layout | Temperature baseline | Second baseline | Third baseline |
|---|---:|---:|---:|
| second only | 81 | 124 | — |
| second + third | 73 | 107 | 135 |

Temperature uses `FreeSansBold24pt7b`; both secondary lines use
`FreeSans12pt7b`. The third baseline is 135 rather than 139 specifically so
descenders such as the `p` in `hPa` reach 139 without crossing the value-block
boundary.

The second and third lines are centred as text-plus-ring groups. For dew point,
the ring is a small `drawCircle` (radius 2, gap 4) anchored to the 12 pt cap
height; it is not printed because the GFX font has no degree glyph.

No truncation is required for the fixed vocabulary: the widest line,
`PM2.5 8 ug/m3`, measures about 155 px and fits the 200 px panel with margins.
A future field that does not fit would be a spec/design extension, not a silent
truncation.

### D4. Warning icon: smaller and anchored to the temperature

The warning icon changes from a fixed 42 px triangle between the values to a
34 px triangle anchored to the temperature baseline:

```text
WARN_APEX_Y      = TEMP_BASELINE_Y - 34
WARN_BASE_Y      = TEMP_BASELINE_Y
WARN_BASE_HALF_W = 17
WARN_BAR_W       = 5
WARN_BAR_TOP_Y   = WARN_APEX_Y + 11
WARN_BAR_BOTTOM_Y = WARN_BASE_Y - 8
WARN_DOT_TOP_Y   = WARN_BASE_Y - 5
WARN_DOT_H       = 3
WARN_LABEL_Y     = TEMP_BASELINE_Y + 6   // built-in font: glyph top
```

The slot centres stay at `x=31` and its mirror at `x=169`.

```text
two-line:   triangle 47..81, label top 87, second line top 108
three-line: triangle 39..73, label top 79, second line top 90
```

This gives the two-line layout about 15 px between the warning label and a long
second line such as `SLP 1013 hPa`, while the three-line layout moves the whole
warning block up with the temperature. The icon remains purely additive: no
value line or footer element shifts when a warning appears or clears.

**Alternatives considered:** keeping the 42 px icon and only moving it in the
three-line layout left the two-line warning label 1 px above a wide second line;
shrinking to 30 px looked too small against the 5x7 label. The 34 px
temperature-anchored geometry was verified with the host renderer using the
real fonts.

### D5. Refresh policy: per-slot availability and hysteresis

`RefreshPolicy` gains a small slot abstraction:

```text
struct ValueSlot {
  float value;
  bool available;
  float hysteresis;
};
```

`evaluate()` takes the temperature slot, the second slot and the third slot,
plus the existing clock, setpoint, control state, demand bucket and warning
token. Temperature uses its existing 0.1 °C hysteresis; field-specific
hysteresis comes from `DisplayField`.

Change detection per slot:
- if availability changed in either direction, the slot is a change;
- else if the slot is available and moves by at least its hysteresis, it is a
  change;
- an unavailable slot is never compared against a stale value.

This is deliberately independent of the old `valid && temperature && humidity`
helper. A pressure-only sensor produces a valid snapshot with no temperature or
humidity; the pressure slot must still repaint while the temperature line shows
a placeholder.

The existing first-paint, minimum-interval, ghosting, clock, setpoint, control,
demand and warning rules are unchanged. Warning onset still bypasses the
minimum-interval floor; slot availability transitions do not.

### D6. API and settings UI use string tokens

The display endpoints expose the tokens, not enum numbers:

```json
{
  "second_field": "humidity",
  "third_field": "none"
}
```

`GET` maps the enum to tokens. `POST` maps known tokens back; an absent key
leaves the stored value alone, and an unknown token falls back to the documented
default during validation. `third_field: null` maps to `none`.

The settings page gains two selects in the E-Paper Display section. Their
option values are the same tokens. The section explains the sea-level-pressure
elevation dependency and that missing sensors render placeholders. The existing
save flow (CSRF header, restart prompt, gzip pre-build hook) is reused.

### D7. Tests and host renderings

- `test/test_config`: enum defaults, load/save round-trip, corrupt fallback and
  token parsing.
- New `test/test_display_fields`: mapping, float/int32 extraction, formatting,
  placeholders, hysteresis and degree-ring flags.
- `test/test_display_refresh_policy`: updated signature plus pressure-only
  sensor, VOC jitter/decisive change, availability transitions and interval-floor
  cases.
- `scripts/render_host/main.cpp` gains second/third line inputs and degree flags;
  samples cover default humidity, dew point, pressure, sea-level pressure and
  VOC index. README mock images are regenerated.
- The native `build_src_filter` gains `+<display/DisplayField.cpp>` and the new
  test directory.

## Risks / Trade-offs

- **Warning icon is less prominent.** The 34 px triangle is smaller than the
  current 42 px icon → verified against the real GFX fonts; the label and
  mirrored slot preserve visibility.
- **Wide labels consume most of the panel width.** `PM2.5 8 ug/m3` is about
  155 px → the fixed vocabulary is measured and fits; no truncation is added.
- **Sea-level pressure silently absent.** Drivers emit it only when elevation
  is configured → the UI states the dependency and the display shows a
  placeholder rather than inventing a value.
- **VOC index updates every second.** Without hysteresis it would pin refreshes
  at the interval floor → use a 5-point threshold and keep the existing minimum
  interval.
- **Duplicate selections.** The same field can be selected for both slots → it
  renders two identical lines; no validation rule is added because there is no
  safety consequence and the user may intentionally mirror a value.
- **Persisted enum values must stay stable.** Append new fields; do not reorder
  existing values. Unknown values fall back safely.
- **Config changes require a restart.** The layout cannot switch live → this
  matches every other display setting and keeps the panel geometry deterministic.

## Migration Plan

- Devices with no display value keys load `humidity` and `none`, preserving the
  current two-line display exactly.
- `GET /api/display` only adds fields; existing clients can ignore them.
- Rollback is safe: older firmware ignores the new NVS keys, and the new
  settings UI is delivered only with the matching firmware.
- No data-partition migration is needed. `src/generated/settings_gz.h` is
  regenerated by the existing compression hook when the UI changes.
