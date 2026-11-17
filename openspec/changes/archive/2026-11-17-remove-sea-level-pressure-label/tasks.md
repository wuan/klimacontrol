# Tasks

## 1. Firmware rendering

- [x] 1.1 In `src/display/DisplayField.cpp`, set the `DisplayField::SeaLevelPressure` label to `""` so the line formats as `1016 hPa` / `-- hPa` while `DisplayField::Pressure` keeps `P`. Verify with the native `test_display_fields` suite after 2.1.
- [x] 1.2 Update the comments that name `SLP` as a wide value: `src/display/EPaperDisplay.cpp` (warning-slot geometry comment) and `src/display/DisplayField.h` / `EPaperDisplay.h` format examples, replacing `SLP ...` with a value the panel still renders. Verify with `grep -rn "SLP" src/` returning nothing.

## 2. Tests

- [x] 2.1 In `test/test_display_fields/test_display_fields.cpp`, update `test_format_pressure_lines` to expect `1016 hPa` for sea-level pressure and `test_placeholders_keep_label_and_unit` to expect `-- hPa`. Run `pio test -e native` and confirm every display-field test passes with `P 1013 hPa` still asserted for local pressure.

## 3. Host renderer and documentation

- [x] 3.1 Update the `sea_level` and `three_line_missing` samples in `scripts/render_host/main.cpp` to `1016 hPa` and `-- hPa`, then regenerate the mock PNGs with `python3 scripts/render_display_mock.py --out docs/images/display`. Verify the regenerated `display_sea_level.png` and `display_three_line_missing.png` render no `SLP` prefix.
- [x] 3.2 Update `README.md`: the sea-level-pressure row description, the missing-readings alt text, and the warning-with-a-long-second-line row that names `SLP 1013 hPa`. Verify with `grep -n "SLP\|sea-level pressure.*SLP" README.md` returning nothing.

## 4. Verification

- [x] 4.1 Run `pio test -e native`, `pio run -e adafruit_qtpy_esp32s2`, and `pio run -e adafruit_qtpy_esp32s2_idf`; all must succeed.
- [x] 4.2 Run `clang-format -i` on every touched C/C++ file under `src/` and `scripts/`, and confirm `git diff --check` is clean.
- [x] 4.3 Run `openspec validate --all` and confirm the change and its delta spec validate.

## Workflow follow-up

- Sync the `display` delta into `openspec/specs/display/spec.md` and archive this change with `/opsx-archive remove-sea-level-pressure-label`.
