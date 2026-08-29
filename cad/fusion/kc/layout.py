"""Positions derived from `kc.params`, and the checks on them.

Deliberately free of any Fusion import, for two reasons: the arithmetic is the
part that is easy to get wrong and expensive to get wrong, and keeping it here
means it can be exercised from a plain interpreter instead of by re-running a
script inside Fusion and reading a message box.

Coordinate system, used by every component builder:

    origin  centre of the frame's 55x55 aperture
    +Z      rearward, into the wall
    +Y      up
    +X      to the LEFT as seen from the front (right-handed, +Z rearward)
    z = 0   front face of the Gira cover frame

Z stack through the front plate, front to back:

    z = -lip_t      front face of the lip, proud of the frame
    z = 0           frame front face
    z = z_glass0    front face of the panel glass  (front_wall behind the lip)
    z = z_plug      rear face of the plug == glass pocket floor

Centring
--------
Three X positions matter and they are all derived from one measurement:

    active_dx   where the image sits on the PCB. A property of the part.
    module_dx   where we put the PCB. A choice.
    window_dx   where the hole goes = active_dx + module_dx, by construction,
                so the window is always concentric with the image.

Only 2.7 mm of module travel exists before the PCB breaks out through the
aperture wall (plug 54.2 wide, PCB 48 + 0.4/side), so a 3 mm offset cannot be
absorbed by the module alone. `module_carry` splits it: the module takes
2.5 mm, leaving the window 0.5 mm off the aperture centre.
"""


def geom(p):
    """Derived X/Y/Z positions. One place, so nothing can drift."""
    # Model +X is to the LEFT from the front, so an FPC on the left is at +X
    # and the image, sitting away from it, is at -X.
    s = -1.0 if p["fpc_side"] == "left" else 1.0

    g = {}
    g["s"] = s
    g["active_dx"] = s * p["active_offset"]
    g["active_dy"] = p["active_offset_y"]
    g["module_dx"] = -s * p["module_carry"]
    g["module_dy"] = -p["active_offset_y"]
    g["window_dx"] = g["active_dx"] + g["module_dx"]
    g["window_dy"] = g["active_dy"] + g["module_dy"]

    g["plug"] = p["aperture"] - 2 * p["aperture_clear"]
    g["lip"] = p["aperture"] + 2 * p["lip_overlap"]
    g["z_lip"] = -p["lip_t"]
    g["z_glass0"] = g["z_lip"] + p["front_wall"]
    g["z_plug"] = g["z_glass0"] + p["disp_glass_t"]

    # The glass pocket, with clearance. Several components need it.
    g["glass_cx"] = g["module_dx"] + p["disp_glass_dx"]
    g["glass_cy"] = g["module_dy"] + p["disp_glass_dy"]
    g["glass_w"] = p["disp_glass_w"] + 2 * p["clear"]
    g["glass_h"] = p["disp_glass_h"] + 2 * p["clear"]

    # The FPC leaves the module at the same end as the header, i.e. the side
    # `s` points away from the image.
    g["fpc_sign"] = -s
    return g


def checks(p, g):
    """Report clearances rather than fail, so a tight fit is a decision."""
    out = []

    pw = p["disp_pcb_w"] + 2 * p["clear"]
    ph = p["disp_pcb_h"] + 2 * p["clear"]

    for axis, ctr, size in (("X", g["module_dx"], pw), ("Y", g["module_dy"], ph)):
        for sgn in (-1, 1):
            gap = g["plug"] / 2 - sgn * ctr - size / 2
            if gap < 0:
                out.append(
                    "module runs {:.2f} mm past the plug edge in {}{}; it "
                    "will not fit the aperture"
                    .format(-gap, "+" if sgn > 0 else "-", axis))
            elif gap < 1.0:
                out.append(
                    "only {:.2f} mm of plug material on the {}{} side of the "
                    "module — no room for a locating rib there"
                    .format(gap, "+" if sgn > 0 else "-", axis))

    # The window is cut through the wall left by the glass pocket, so it has
    # to stay inside that pocket or it breaks out through the plug face.
    for axis, wc, pc, ps in (("X", g["window_dx"], g["glass_cx"], g["glass_w"]),
                             ("Y", g["window_dy"], g["glass_cy"], g["glass_h"])):
        over = abs(wc - pc) + p["window"] / 2 - ps / 2
        if over > 0:
            out.append(
                "window escapes the glass pocket by {:.2f} mm in {} — it "
                "would break out through the plug face".format(over, axis))

    reveal = (p["window"] - p["disp_active"]) / 2
    if reveal < 0:
        out.append("window is smaller than the active area by {:.2f} mm/side"
                   .format(-reveal))

    return out


def report(p, g):
    """Human-readable summary of where everything landed."""
    reveal = (p["window"] - p["disp_active"]) / 2
    side = "left" if g["s"] < 0 else "right"
    img = "right" if g["s"] < 0 else "left"
    return "\n".join([
        "FPC/header end: {} (viewed from the front)".format(side),
        "image sits {:.1f} mm to the {} of the module centre"
        .format(p["active_offset"], img),
        "",
        "module PCB centre  x = {:+.2f}  y = {:+.2f}  (model)"
        .format(g["module_dx"], g["module_dy"]),
        "viewport centre    x = {:+.2f}  y = {:+.2f}  (model)"
        .format(g["window_dx"], g["window_dy"]),
        "viewport offset in the bezel: {:.2f} mm to the {}"
        .format(abs(g["window_dx"]),
                img if g["window_dx"] * g["s"] > 0 else side),
        "bezel border, {} / {}: {:.2f} / {:.2f} mm"
        .format(side, img,
                p["aperture"] / 2 + g["s"] * g["window_dx"] - p["window"] / 2,
                p["aperture"] / 2 - g["s"] * g["window_dx"] - p["window"] / 2),
        "reveal round the active area: {:.2f} mm/side".format(reveal),
        "glass front face at z = {:.2f}, plug rear face at z = {:.2f}"
        .format(g["z_glass0"], g["z_plug"]),
    ])


def summary(p):
    """report() plus checks(), the text the entry point shows and logs."""
    g = geom(p)
    warnings = checks(p, g)
    tail = ("\n\nWARNINGS:\n- " + "\n- ".join(warnings)) if warnings \
        else "\n\nNo warnings."
    return report(p, g) + tail


if __name__ == "__main__":
    # Runs outside Fusion: `python3 cad/fusion/kc/layout.py`
    from params import P
    print(summary(P))
