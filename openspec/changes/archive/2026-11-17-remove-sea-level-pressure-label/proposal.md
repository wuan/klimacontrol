# Proposal

## Why

The e-paper value lines already carry the `hPa` unit, which identifies the
reading as a pressure. The `SLP` prefix on the sea-level pressure line repeats
that information and widens an already wide line, so it earns no space on a
200 px panel. The `P` marker stays only where it disambiguates: on the local
pressure line.

## What Changes

- Sea-level pressure renders as the number and the `hPa` unit only — `1016 hPa`
  when available, `-- hPa` when unavailable — with no `SLP` prefix.
- Local pressure keeps its `P` marker: `P 1013 hPa`.
- The warning-icon scenario's example of a wide second value no longer names a
  line the panel can produce, so it moves to a value that still occurs.
- The README mock-display table and the host-rendered mock images are updated to
  match the new rendering.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `display`: the "Display field labels" requirement drops the `SLP` marker from
  sea-level pressure; the "Warning icon layout" long-second-line scenario uses a
  value the panel still renders.

## Impact

- `src/display/DisplayField.cpp` — sea-level pressure field spec label.
- `src/display/EPaperDisplay.cpp` — comment naming `SLP` as a wide value.
- `test/test_display_fields/test_display_fields.cpp` — formatted-line and
  placeholder expectations.
- `scripts/render_host/main.cpp` and `docs/images/display/*` — mock samples.
- `README.md` — mock-display table descriptions and image alt text.

No configuration keys, HTTP fields, or UI options change: the token remains
`sea_level_pressure` and the settings label remains "Sea-level pressure".
