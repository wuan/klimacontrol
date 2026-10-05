#!/usr/bin/env python3
"""Render mock PNGs of the e-paper panel layout for design review.

Parses the layout constants out of src/display/EPaperDisplay.cpp so the mock
stays in step with the firmware drawing code, then draws sample screens with
PIL. This is a REVIEW AID, not a simulator: fonts are approximated with
system TrueType fonts and 1-bit dithering/GxEPD2 paging is not modelled.
Pixel-exact checking happens on hardware (change add-display-warning-icon,
task 5.2).

Usage:
    python3 scripts/render_display_mock.py [--out DIR] [--src EPaperDisplay.cpp]

Outputs one PNG per sample state (normal, each warning token, etc.).
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    sys.exit("Pillow is required: pip install pillow")

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = REPO_ROOT / "src" / "display" / "EPaperDisplay.cpp"

WHITE = 255
BLACK = 0

# macOS / Linux font candidates, best first. FreeSans metrics differ slightly
# from these, which is acceptable for a review aid.
FONT_CANDIDATES = {
    "sans": [
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    ],
    "sans-bold": [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    ],
}


def load_font(kind: str, size: int):
    for path in FONT_CANDIDATES[kind]:
        try:
            return ImageFont.truetype(path, size)
        except OSError:
            continue
    return ImageFont.load_default(size)


def parse_constants(src_path: Path) -> dict:
    """Extract constexpr int16_t constants (and simple derived expressions)."""
    text = src_path.read_text()
    # PANEL_W/H come from GxEPD2_154_D67::WIDTH/HEIGHT; DEMAND_BUCKETS from
    # RefreshPolicy.h. Values are fixed for this panel.
    consts: dict = {"PANEL_W": 200, "PANEL_H": 200, "DEMAND_BUCKETS": 5}
    pattern = re.compile(r"constexpr int16_t (\w+)\s*=\s*([^;]+);", re.DOTALL)
    for name, expr in pattern.findall(text):
        expr = re.sub(r"\s+", " ", expr.replace("Display::", "")).strip()
        try:
            consts[name] = int(eval(expr, {"__builtins__": {}}, dict(consts)))  # noqa: S307
        except Exception:
            pass  # derived constant referencing something we did not parse
    return consts


# The warning-icon geometry lives in EPaperDisplay.cpp; these mirror the
# parse. Kept here only as fallbacks if the parse misses a constant.
WARN_DEFAULTS = {
    "WARN_CX": 31,
    "WARN_APEX_Y": 48,
    "WARN_BASE_Y": 90,
    "WARN_BASE_HALF_W": 21,
    "WARN_BAR_W": 5,
    "WARN_BAR_TOP_Y": 64,
    "WARN_BAR_BOTTOM_Y": 78,
    "WARN_DOT_TOP_Y": 82,
    "WARN_DOT_H": 3,
    "WARN_LABEL_Y": 100,
}

WARNING_LABELS = {
    "NONE": "",
    "OVERHEAT": "OVERHEAT",
    "FROST": "FROST",
    "SENSOR": "SENSOR",
    "ACTUATOR": "ACTUATOR",
    "HUMID": "HUMID",
}


def draw_panel(consts: dict, warning: str, temp: str, hum: str) -> Image.Image:
    c = {**WARN_DEFAULTS, **consts}
    img = Image.new("L", (c["PANEL_W"], c["PANEL_H"]), WHITE)
    d = ImageDraw.Draw(img)

    font_builtin = load_font("sans", 8)   # ~5x7 built-in font
    font_temp = load_font("sans-bold", 32)  # FreeSansBold24pt
    font_hum = load_font("sans", 16)      # FreeSans12pt
    font_footer = load_font("sans", 12)   # FreeSans9pt

    # Header band (constant content only)
    d.text((c["FOOTER_MARGIN_X"], 2), "KlimaControl", font=font_builtin, fill=BLACK)
    d.text((c["PANEL_W"] - 40, 2), "v0.1.1", font=font_builtin, fill=BLACK)

    # Temperature, centred with a drawn degree ring
    tw = d.textlength(temp, font=font_temp)
    degree_adv = c["DEGREE_GAP"] + 2 * c["DEGREE_RADIUS"]
    x = (c["PANEL_W"] - (tw + degree_adv)) / 2
    d.text((x, c["TEMP_BASELINE_Y"] - 30), temp, font=font_temp, fill=BLACK)
    ring_cx = x + tw + c["DEGREE_GAP"] + c["DEGREE_RADIUS"]
    ring_cy = c["TEMP_BASELINE_Y"] - 30 + 8 + c["DEGREE_RADIUS"]
    for r in (c["DEGREE_RADIUS"] - 1, c["DEGREE_RADIUS"], c["DEGREE_RADIUS"] + 1):
        d.ellipse([ring_cx - r, ring_cy - r, ring_cx + r, ring_cy + r], outline=BLACK)

    # Humidity, centred
    hum_line = f"{hum} %rH"
    hw = d.textlength(hum_line, font=font_hum)
    d.text(((c["PANEL_W"] - hw) / 2, c["HUMIDITY_BASELINE_Y"] - 14), hum_line, font=font_hum, fill=BLACK)

    # Warning slot (left margin)
    if warning != "NONE":
        apex = (c["WARN_CX"], c["WARN_APEX_Y"])
        base_l = (c["WARN_CX"] - c["WARN_BASE_HALF_W"], c["WARN_BASE_Y"])
        base_r = (c["WARN_CX"] + c["WARN_BASE_HALF_W"], c["WARN_BASE_Y"])
        d.polygon([apex, base_l, base_r], fill=BLACK)
        d.rectangle(
            [c["WARN_CX"] - c["WARN_BAR_W"] // 2, c["WARN_BAR_TOP_Y"],
             c["WARN_CX"] + c["WARN_BAR_W"] // 2, c["WARN_BAR_BOTTOM_Y"]],
            fill=WHITE,
        )
        d.rectangle(
            [c["WARN_CX"] - c["WARN_BAR_W"] // 2, c["WARN_DOT_TOP_Y"],
             c["WARN_CX"] + c["WARN_BAR_W"] // 2, c["WARN_DOT_TOP_Y"] + c["WARN_DOT_H"] - 1],
            fill=WHITE,
        )
        label = WARNING_LABELS[warning]
        lw = d.textlength(label, font=font_builtin)
        d.text((c["WARN_CX"] - lw / 2, c["WARN_LABEL_Y"]), label, font=font_builtin, fill=BLACK)

    # Footer
    d.line(
        [c["FOOTER_MARGIN_X"], c["FOOTER_RULE_Y"], c["PANEL_W"] - c["FOOTER_MARGIN_X"], c["FOOTER_RULE_Y"]],
        fill=BLACK,
    )
    d.text((c["FOOTER_MARGIN_X"], c["FOOTER_LINE1_Y"] - 12), "klima-aabbcc", font=font_footer, fill=BLACK)
    d.text((c["FOOTER_MARGIN_X"], c["FOOTER_LINE2_Y"] - 12), "26-10-05 14:07", font=font_footer, fill=BLACK)

    # Setpoint + degree ring, right-aligned
    setpoint = "22.0"
    sw = d.textlength(setpoint, font=font_footer)
    ring_cx = c["FOOTER_RIGHT_X"] - c["SETPOINT_DEGREE_RADIUS"]
    ring_cy = c["FOOTER_LINE1_Y"] - 10
    d.ellipse(
        [ring_cx - c["SETPOINT_DEGREE_RADIUS"], ring_cy - c["SETPOINT_DEGREE_RADIUS"],
         ring_cx + c["SETPOINT_DEGREE_RADIUS"], ring_cy + c["SETPOINT_DEGREE_RADIUS"]],
        outline=BLACK,
    )
    d.text((ring_cx - c["SETPOINT_DEGREE_RADIUS"] - c["SETPOINT_DEGREE_GAP"] - sw, c["FOOTER_LINE1_Y"] - 12),
           setpoint, font=font_footer, fill=BLACK)

    # Control symbol: filled circle (heating) on footer line 2
    sym_cx = c["FOOTER_RIGHT_X"] - c["CONTROL_SYMBOL_RADIUS"]
    sym_cy = c["CONTROL_SYMBOL_CY"]
    d.ellipse(
        [sym_cx - c["CONTROL_SYMBOL_RADIUS"], sym_cy - c["CONTROL_SYMBOL_RADIUS"],
         sym_cx + c["CONTROL_SYMBOL_RADIUS"], sym_cy + c["CONTROL_SYMBOL_RADIUS"]],
        fill=BLACK,
    )

    # Demand bar, left of the symbol
    bar_right = sym_cx - c["CONTROL_SYMBOL_RADIUS"] - c["DEMAND_BAR_GAP"]
    top = c["FOOTER_LINE2_Y"] - c["DEMAND_SEG_H"]
    for i in range(5):
        x0 = bar_right - c["DEMAND_BAR_W"] + i * (c["DEMAND_SEG_W"] + c["DEMAND_SEG_GAP"])
        box = [x0, top, x0 + c["DEMAND_SEG_W"], top + c["DEMAND_SEG_H"]]
        if i < 3:
            d.rectangle(box, fill=BLACK)
        else:
            d.rectangle(box, outline=BLACK)

    return img


SAMPLES = [
    ("normal", "NONE", "21.4", "45"),
    ("frost", "FROST", "2.8", "52"),
    ("overheat", "OVERHEAT", "31.6", "40"),
    ("sensor", "SENSOR", "--.-", "--"),
    ("actuator", "ACTUATOR", "21.4", "45"),
    ("humid", "HUMID", "21.4", "78"),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", type=Path, default=DEFAULT_SRC)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "build" / "mock_display")
    args = parser.parse_args()

    consts = parse_constants(args.src)
    args.out.mkdir(parents=True, exist_ok=True)

    for name, warning, temp, hum in SAMPLES:
        img = draw_panel(consts, warning, temp, hum)
        out = args.out / f"display_{name}.png"
        img.save(out)
        print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
