"""The front plate.

Step 1 of the rebuild: the viewport and nothing else. Later steps add the
PCB-locating rib, the antenna pocket, the clamp posts and the vent grille.
"""

from .primitives import CUT, JOIN, NEW, boxc, rect


def build(comp, p, g):
    # Lip, proud of the frame's front face, then the plug filling the aperture.
    boxc(comp, 0, 0, g["lip"], g["lip"], g["z_lip"], 0.0, NEW)
    boxc(comp, 0, 0, g["plug"], g["plug"], 0.0, g["z_plug"], JOIN)

    # Pocket for the panel glass, cut into the plug's rear face. The module
    # drops in here glass-first.
    boxc(comp, g["glass_cx"], g["glass_cy"], g["glass_w"], g["glass_h"],
         g["z_glass0"], g["z_plug"], CUT)

    # Relief so the FPC can fold around the module's edge, on the same end as
    # the header. Only as wide as ribbon_gap, so material is left further out
    # for the locating features a later step adds.
    fs = g["fpc_sign"]
    x_edge = g["glass_cx"] + fs * (g["glass_w"] / 2 - 0.1)  # 0.1 back into the
    # pocket, so the two cuts overlap instead of merely touching
    rect(comp, x_edge, g["glass_cy"] - p["ribbon_gap"] / 2,
         x_edge + fs * p["ribbon_relief"], g["glass_cy"] + p["ribbon_gap"] / 2,
         g["z_glass0"], g["z_plug"], CUT)

    # The viewport. Concentric with the active area by construction, not with
    # the aperture — see the "Centring" section in kc.layout.
    boxc(comp, g["window_dx"], g["window_dy"], p["window"], p["window"],
         g["z_lip"] - 0.5, g["z_glass0"] + 0.05, CUT)
