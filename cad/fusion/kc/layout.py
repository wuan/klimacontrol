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
The PCB and the panel glass are centred on each other. The ACTIVE AREA is not
centred on the glass: it sits `active_offset` (3 mm) away from the ribbon end,
along the module's long side.

The image is the one thing the room ever looks at, so the image is what gets
centred on the aperture. Everything that holds the module therefore comes out
off-centre by the same 3 mm — the glass pocket, and every locating feature a
later step adds.

Three X positions, all derived from that one measurement:

    active_dx   where the image sits on the module. A property of the part.
    module_dx   where we put the module = -active_dx, so the image lands on
                the aperture centre. Scaled by `module_carry`.
    window_dx   where the hole goes = active_dx + module_dx, by construction,
                so the window is always concentric with the image. Zero when
                module_carry == active_offset.

How much room the shift has: the module lives BEHIND the plug's rear face, so
what bounds it is the 55 mm aperture, not the plug's 54.2 mm. At the full 3 mm
the 48 mm PCB spans -21.0..27.0 against +/-27.5 — it fits, with 0.5 mm to
spare, and that 0.5 mm is why the locating rib has to be one-sided.
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

    # Everything behind the plug's rear face has to pass through the aperture
    # with the module already fitted, so this — not the plug — is what bounds
    # the rib and anything else built back there.
    g["envelope"] = p["aperture"] / 2 - p["clear"]

    # PCB edges with clearance: the rib's inner faces.
    g["pcb_hx"] = p["disp_pcb_w"] / 2 + p["clear"]
    g["pcb_hy"] = p["disp_pcb_h"] / 2 + p["clear"]

    g["pin_dia"] = p["disp_hole_dia"] - p["pin_clear"]
    g["pins"] = [(g["module_dx"] + sx * p["disp_hole_pitch_x"] / 2,
                  g["module_dy"] + sy * p["disp_hole_pitch_y"] / 2)
                 for sx in (-1, 1) for sy in (-1, 1)]

    g["rib_segments"], g["rib_notes"] = _rib_sides(p, g)
    return g


def _rib_sides(p, g):
    """Which rib segments fit, as (x0, y0, x1, y1) rectangles, plus notes.

    A side is dropped when the aperture wall leaves less than a printable
    wall's worth of room between it and the PCB. With the module carried the
    full 3 mm that is exactly what happens on the ribbon side, so the rib
    comes out three-sided — which is convenient, because that is the side the
    ribbon has to escape from anyway.
    """
    env = g["envelope"]
    segments, notes = [], []

    # Extent of the rib along the side it runs, corners included, clamped to
    # the aperture so a corner cannot poke out past the module.
    span_x = (max(g["module_dx"] - g["pcb_hx"] - p["rib_w"], -env),
              min(g["module_dx"] + g["pcb_hx"] + p["rib_w"], env))
    span_y = (max(g["module_dy"] - g["pcb_hy"] - p["rib_w"], -env),
              min(g["module_dy"] + g["pcb_hy"] + p["rib_w"], env))

    for axis, ctr, half in (("X", g["module_dx"], g["pcb_hx"]),
                            ("Y", g["module_dy"], g["pcb_hy"])):
        for sgn in (-1, 1):
            inner = ctr + sgn * half
            outer = ctr + sgn * (half + p["rib_w"])
            # Clamp the outer face into the aperture, then see what is left.
            outer = max(-env, min(env, outer))
            width = abs(outer - inner) if sgn * (outer - inner) > 0 else 0.0
            if width < MIN_WALL:
                notes.append(
                    "no rib on {}: {:.2f} mm to the aperture wall, need {:.1f}"
                    .format(_side(sgn, axis), width, MIN_WALL))
                continue
            if width < p["rib_w"] - 1e-9:
                notes.append(
                    "rib on {} thinned to {:.2f} mm (rib_w is {:.2f}) to stay "
                    "inside the aperture"
                    .format(_side(sgn, axis), width, p["rib_w"]))
            lo, hi = min(inner, outer), max(inner, outer)
            if axis == "X":
                segments.append((lo, span_y[0], hi, span_y[1]))
            else:
                segments.append((span_x[0], lo, span_x[1], hi))
    return segments, notes


# Thinnest wall worth printing at 0.4 mm nozzle / 0.15 mm layers. Used to
# decide whether a locating feature will fit somewhere, not to size one.
MIN_WALL = 0.8


def _side(sgn, axis):
    return "{}{}".format("+" if sgn > 0 else "-", axis)


def checks(p, g):
    """Report clearances rather than fail, so a tight fit is a decision."""
    out = []

    # --- the module, behind the plug's rear face --------------------------
    # The reference here is the APERTURE, not the plug. The plug is 54.2 only
    # because it needs a sliding fit in the 55 mm hole, and it ends at
    # z_plug; the module sits behind that face, where the nearest thing it
    # can foul is the aperture wall itself. Measuring the module against the
    # plug width understates the room by 0.8 mm and wrongly rules out shifts
    # that in fact fit.
    for axis, ctr, size in (("X", g["module_dx"], p["disp_pcb_w"]),
                            ("Y", g["module_dy"], p["disp_pcb_h"])):
        for sgn in (-1, 1):
            gap = p["aperture"] / 2 - sgn * ctr - size / 2
            if gap < 0:
                out.append(
                    "module fouls the aperture wall by {:.2f} mm on {}; the "
                    "faceplate cannot be inserted with the module fitted"
                    .format(-gap, _side(sgn, axis)))
            elif gap < p["clear"]:
                out.append(
                    "module edge is {:.2f} mm from the aperture wall on {} — "
                    "effectively flush, with no clearance to insert it"
                    .format(gap, _side(sgn, axis)))
            # Whether a rib fits in what is left is _rib_sides()' business,
            # reported through g["rib_notes"] below.

    # --- the glass pocket, cut into the plug ------------------------------
    # This one IS bounded by the plug, because it is a pocket in it.
    for axis, ctr, size in (("X", g["glass_cx"], g["glass_w"]),
                            ("Y", g["glass_cy"], g["glass_h"])):
        for sgn in (-1, 1):
            gap = g["plug"] / 2 - sgn * ctr - size / 2
            if gap < 0:
                out.append(
                    "glass pocket breaks out through the plug's {} face by "
                    "{:.2f} mm".format(_side(sgn, axis), -gap))
            elif gap < 1.0:
                out.append(
                    "only {:.2f} mm of plug wall left beside the glass pocket "
                    "on {}".format(gap, _side(sgn, axis)))

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

    # --- locating pins ----------------------------------------------------
    # A pin is JOINed to the plug's rear face, so it needs material under it.
    # Over the glass pocket there is none — the pocket is cut right through
    # that Z band — and the pin would be extruded into thin air.
    r = g["pin_dia"] / 2
    for px, py in g["pins"]:
        if (abs(px - g["glass_cx"]) < g["glass_w"] / 2 + r
                and abs(py - g["glass_cy"]) < g["glass_h"] / 2 + r):
            out.append(
                "locating pin at ({:+.1f}, {:+.1f}) lands on the glass pocket, "
                "where there is no material to stand it on — check "
                "disp_hole_pitch_x/y".format(px, py))
        if abs(px) + r > g["envelope"] or abs(py) + r > g["envelope"]:
            out.append(
                "locating pin at ({:+.1f}, {:+.1f}) is outside the aperture"
                .format(px, py))

    if p["disp_hole_pitch_x"] >= p["disp_pcb_w"] \
            or p["disp_hole_pitch_y"] >= p["disp_pcb_h"]:
        out.append("hole pitch is wider than the PCB — disp_hole_pitch_x/y "
                   "are centre-to-centre, not edge-to-edge")

    if p["pin_h"] >= p["disp_pcb_t"]:
        out.append(
            "pin_h {:.2f} >= PCB thickness {:.2f}; a proud pin will hold the "
            "module off the plug face"
            .format(p["pin_h"], p["disp_pcb_t"]))

    # --- the rib ----------------------------------------------------------
    out.extend(g["rib_notes"])
    if len(g["rib_segments"]) < 2:
        out.append("rib has {} side(s) — too few to locate anything"
                   .format(len(g["rib_segments"])))

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
        "viewport in the bezel: {}".format(
            "centred" if abs(g["window_dx"]) < 5e-3 else
            "{:.2f} mm to the {}".format(
                abs(g["window_dx"]),
                img if g["window_dx"] * g["s"] > 0 else side)),
        "bezel border, {} / {}: {:.2f} / {:.2f} mm"
        .format(side, img,
                p["aperture"] / 2 + g["s"] * g["window_dx"] - p["window"] / 2,
                p["aperture"] / 2 - g["s"] * g["window_dx"] - p["window"] / 2),
        "reveal round the active area: {:.2f} mm/side".format(reveal),
        "",
        "module to aperture wall, {} / {}: {:.2f} / {:.2f} mm"
        .format(side, img,
                p["aperture"] / 2 + g["s"] * g["module_dx"] - p["disp_pcb_w"] / 2,
                p["aperture"] / 2 - g["s"] * g["module_dx"] - p["disp_pcb_w"] / 2),
        "plug wall beside the glass pocket, {} / {}: {:.2f} / {:.2f} mm"
        .format(side, img,
                g["plug"] / 2 + g["s"] * g["glass_cx"] - g["glass_w"] / 2,
                g["plug"] / 2 - g["s"] * g["glass_cx"] - g["glass_w"] / 2),
        "glass front face at z = {:.2f}, plug rear face at z = {:.2f}"
        .format(g["z_glass0"], g["z_plug"]),
        "",
        "rib: {} of 4 sides, {:.2f} mm tall".format(
            len(g["rib_segments"]), p["rib_h"]),
        "pins: {} x dia {:.2f} in dia {:.2f} holes ({:.2f} mm fit), "
        "{:.2f} mm tall".format(
            len(g["pins"]), g["pin_dia"], p["disp_hole_dia"],
            p["pin_clear"], p["pin_h"]),
        "pin centres: " + ", ".join("({:+.1f}, {:+.1f})".format(x, y)
                                    for x, y in g["pins"]),
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
