"""Every dimension in the model, in millimetres.

This is the only file where a number is typed. Anything computable from these
lives in `kc.layout`; anything that reads a number straight out of a component
builder instead of from here is a bug waiting to drift.

Entries marked MEASURE are guesses that need a caliper on the real part before
you print.
"""

P = {
    # ---- Gira System 55, Standard 55 cover frame -------------------------
    # 55x55 aperture is the System 55 standard.
    "aperture": 55.0,
    "aperture_clear": 0.4,      # per-side gap of the plug in the aperture
    "lip_overlap": 1.0,         # lip coverage onto the frame's front face
    "lip_t": 0.8,

    # ---- Waveshare 1.54" e-Paper Module (V2, SSD1681) --------------------
    # The module's long side runs along X: 48 mm wide, 33 mm tall.
    "disp_pcb_w": 48.0,         # module outline, per Waveshare wiki
    "disp_pcb_h": 33.0,
    "disp_pcb_t": 1.6,
    "disp_glass_w": 37.4,       # panel glass outline
    "disp_glass_h": 31.8,
    "disp_glass_t": 1.4,        # pocket depth for the glass
    "disp_active": 27.6,        # active area, square

    # MEASURE: glass centre relative to the PCB centre. Only sizes the
    # pocket; it does not move the viewport.
    "disp_glass_dx": 0.0,
    "disp_glass_dy": 0.0,

    # ---- centring the image ---------------------------------------------
    # The active area is not centred on the PCB: it sits `active_offset` away
    # from the FPC/header end, along the module's long side. See the module
    # docstring of `kc.layout` for how the offset is split.
    "fpc_side": "left",         # FPC/header end, as seen from the front
    "active_offset": 3.0,       # active-area centre, away from the FPC end
    "active_offset_y": 0.0,     # MEASURE: same thing on the short side
    "module_carry": 2.5,        # of active_offset, absorbed by moving the
                                # module; the rest moves the window

    "window": 29.0,             # 27.6 active + 0.7 reveal per side
    "front_wall": 1.6,          # material between the room and the glass
    "ribbon_relief": 2.0,       # extra pocket for the FPC fold
    "ribbon_gap": 26.0,         # width of that relief, along the FPC edge

    "clear": 0.4,               # generic print clearance, per side
}
