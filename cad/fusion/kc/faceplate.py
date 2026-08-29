"""The front plate.

Step 1 of the rebuild: the viewport and nothing else. Later steps add the
PCB-locating rib, the antenna pocket, the clamp posts and the vent grille.
"""

from .primitives import CUT, JOIN, NEW, boxc, circ, rect


def build(comp, p, g):
    # The plug, filling the aperture. No lip: the front face sits flush with
    # the frame's, so the plug is the whole outline.
    boxc(comp, 0, 0, g["plug"], g["plug"], g["z_front"], g["z_plug"], NEW)

    # Rear wall: the plug's outer face carried rearward as a skirt, turning
    # the plate into a shallow tray. Built as four rectangles rather than an
    # outer box minus an inner cut, because that cut would span the whole
    # interior and would swallow the rib and the pins if it ever ran after
    # them. Four joins are order-independent.
    o, i = g["wall_outer"], g["wall_inner"]
    for x0, y0, x1, y1 in ((-o, i, o, o), (-o, -o, o, -i),
                           (-o, -i, -i, i), (i, -i, o, i)):
        rect(comp, x0, y0, x1, y1, g["z_plug"], g["z_wall"], JOIN)

    # Pocket for the panel glass, cut into the plug's rear face. The module
    # drops in here glass-first.
    boxc(comp, g["glass_cx"], g["glass_cy"], g["glass_w"], g["glass_h"],
         g["z_glass0"], g["z_plug"], CUT)

    # Relief so the FPC can fold around the module's edge, on the same end as
    # the ribbon. Only as wide as ribbon_gap, so material is left either side
    # of it for the rib's corners. Extents come from kc.layout, which checks
    # them against the plug.
    rect(comp, *g["relief"], g["z_glass0"], g["z_plug"], CUT)

    # The viewport. Concentric with the active area by construction, not with
    # the aperture — see the "Centring" section in kc.layout.
    boxc(comp, g["window_dx"], g["window_dy"], p["window"], p["window"],
         g["z_front"] - 0.5, g["z_glass0"] + 0.05, CUT)

    # Rib round the PCB edge, standing off the plug's rear face. Built from
    # whichever sides `_rib_sides` found room for — with the module carried
    # the full 3 mm the ribbon side is dropped, so this comes out three-sided.
    z_rib = g["z_plug"] + p["rib_h"]
    for x0, y0, x1, y1 in g["rib_segments"]:
        rect(comp, x0, y0, x1, y1, g["z_plug"], z_rib, JOIN)

    # Locating pins through the module's own mounting holes. These, not the
    # rib, are what actually position the module; the rib is a loose fence.
    # They stop short of the PCB's rear face so they can never hold it off
    # the plug.
    for px, py in g["pins"]:
        circ(comp, px, py, g["pin_dia"], g["z_plug"], g["z_plug"] + p["pin_h"],
             JOIN)
