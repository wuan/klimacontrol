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
    # Per-side gap of the plug in the aperture. With no lip this gap is
    # VISIBLE from the front as a reveal all round, so it is now a cosmetic
    # dimension as well as a fit one.
    "aperture_clear": 0.2,

    # ---- Waveshare 1.54" e-Paper Module (V2, SSD1681) --------------------
    # The module's long side runs along X: 48 mm wide, 33 mm tall.
    "disp_pcb_w": 48.0,         # module outline, per Waveshare wiki
    "disp_pcb_h": 33.0,
    "disp_pcb_t": 1.6,
    "disp_glass_w": 37.4,       # panel glass outline
    "disp_glass_h": 31.8,
    "disp_glass_t": 1.4,        # pocket depth for the glass
    "disp_active": 27.6,        # active area, square

    # The glass IS centred on the PCB. What is off-centre is the active area
    # on the glass — see `active_offset` below. Left as parameters because the
    # module is unmeasured in this respect; they only size and place the glass
    # pocket, they do not move the viewport.
    "disp_glass_dx": 0.0,
    "disp_glass_dy": 0.0,

    # ---- centring the image ---------------------------------------------
    # PCB and glass are centred on each other; the active area sits
    # `active_offset` away from the ribbon/FPC end, along the module's long
    # side. The image is what the room looks at, so the image is what gets
    # centred on the aperture — which means the PCB and glass pockets are the
    # things that end up off-centre. See the `kc.layout` module docstring.
    # The module is carried by exactly this much, in the opposite direction,
    # so the image lands dead centre. There is no Y equivalent: the active
    # area is centred on the module on the short side.
    "fpc_side": "left",         # ribbon/FPC end, as seen from the front
    "active_offset": 2.0,       # active-area centre, away from the ribbon end

    # ---- rear wall --------------------------------------------------------
    # A skirt continuing the plug's outer face rearward, so the plate becomes
    # a shallow tray rather than a flat disc. Its inner face is what now
    # bounds everything behind the plug — module, rib, pins.
    "wall_t": 1.0,
    "wall_h": 10.0,             # provisional

    "window": 29.0,             # 27.6 active + 0.7 reveal per side
    "front_wall": 1.6,          # material between the room and the glass
    "ribbon_relief": 3.0,       # how far past the glass pocket it reaches
    "ribbon_gap": 22.0,         # width of that relief, along the FPC edge

    # ---- locating the module PCB -----------------------------------------
    # A rib round the PCB edge, plus four pins through the module's own
    # mounting holes. The pins do the precise locating; the rib is a loose
    # perimeter that stops the module wandering while you close the case, so
    # it is deliberately built with `clear` per side rather than a press fit —
    # two things fighting over the same location is how parts crack.
    "rib_w": 1.7,
    "rib_h": 2.4,               # 0.8 proud of the PCB's rear face

    # MEASURE, ALL FOUR — these are ESTIMATES, not data. Waveshare does not
    # dimension the mounting holes in the wiki drawing, so pitch and diameter
    # below are read off the board photo assuming 3 mm corner insets and M2
    # clearance holes. Locating pins on the wrong pitch are worse than no pins
    # at all: they hold the module off the plug face and it rocks.
    #
    # How to measure: caliper hole centre to hole centre along the long side
    # (pitch_x) and the short side (pitch_y), and the hole diameter itself.
    "disp_hole_pitch_x": 43.0,
    "disp_hole_pitch_y": 28.0,
    "disp_hole_dia": 2.5,
    "pin_clear": 0.25,          # diametral, pin to hole. A locating fit, so
                                # much tighter than the generic `clear`.
    "pin_h": 2,                 # under disp_pcb_t (1.6) so a proud pin can
                                # never hold the PCB off the plug face

    "clear": 0.4,               # generixc print clearance, per side
}
