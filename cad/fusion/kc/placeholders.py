"""Stand-ins for the boards, placed exactly where the case expects them.

Not models of the real parts: each is the board outline plus only the tall
features that can collide. Use them with Inspect -> Interference, then hide or
delete the components before exporting anything for print.
"""

from .primitives import NEW, boxc


def display(comp, p, g):
    # panel glass, sitting in the pocket
    boxc(comp, g["glass_cx"], g["glass_cy"],
         p["disp_glass_w"], p["disp_glass_h"], g["z_glass0"], g["z_plug"], NEW)

    # module PCB, immediately behind the plug
    boxc(comp, g["module_dx"], g["module_dy"], p["disp_pcb_w"], p["disp_pcb_h"],
         g["z_plug"], g["z_plug"] + p["disp_pcb_t"], NEW)

    # Active area, as a thin witness pad in front of the glass. The window is
    # placed from the same numbers, so a visible mismatch between the two is a
    # mistake in `window`, not in the module placement.
    boxc(comp, g["window_dx"], g["window_dy"],
         p["disp_active"], p["disp_active"],
         g["z_glass0"] - 0.2, g["z_glass0"], NEW)
