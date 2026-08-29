"""KlimaControl — Gira System 55 flush-mount case.

Autodesk Fusion script. Builds two printable solids in a new design:

  faceplate  the visible part. Plugs into the 55x55 aperture of a Gira
             System 55 cover frame, carries the Waveshare 1.54" e-paper
             module and the sensor vent grille.
  body       the rear part. Screws to a standard round flush-mount box
             (60 mm screw centres), carries the QT Py ESP32-S2 and the
             SHT41, and clamps the Gira frame against the faceplate lip.

Run it from  Utilities -> Add-Ins -> Scripts and Add-Ins -> Scripts -> "+".

Coordinate system, used consistently below:

    origin  centre of the frame's 55x55 aperture
    +Z      rearward, into the wall
    z = 0   front face of the Gira cover frame
    z = FRAME_CLAMP_T   rear face of the Gira frame == wall surface

Everything is driven by the P dict. Edit a value, re-run the script, get a
new design; the script does not create Fusion user parameters, so the
model is not editable from the Parameters dialog. Entries marked MEASURE
are guesses that need a caliper on the real part before you print.
"""

import os
import traceback

import adsk.core
import adsk.fusion

# Fusion's API is unitless-internal in centimetres. Every dimension in this
# file is in millimetres and goes through vi()/pt(), which apply the factor.
MM = 0.1

# Build simplified stand-ins for the three boards alongside the case. Set to
# False once you are done checking clearances.
PLACEHOLDERS = True

P = {
    # ---- Gira System 55, Standard 55 cover frame -------------------------
    # 55x55 aperture is the System 55 standard. Outer 80.7 x 80.7 x 11.4 mm
    # is from the Standard 55 data sheet (Gira art. 021103).
    "aperture": 55.0,
    "aperture_clear": 0.4,      # per-side gap of the plug in the aperture
    # MEASURE: front-to-rear thickness of the frame where we clamp it. The
    # data sheet's 11.4 mm is the overall depth; if the frame has a rear
    # collar entering the box, the clamping thickness is less.
    "frame_clamp_t": 11.4,
    "lip_overlap": 1.0,         # lip coverage onto the frame's front face
    "lip_t": 0.8,

    # ---- Waveshare 1.54" e-Paper Module (V2, SSD1681) --------------------
    "disp_pcb_w": 48.0,         # module outline, per Waveshare wiki
    "disp_pcb_h": 33.0,
    "disp_pcb_t": 1.6,
    "disp_glass_w": 37.4,       # panel glass outline
    "disp_glass_h": 31.8,
    "disp_glass_t": 1.4,        # pocket depth for the glass
    # MEASURE: glass centre and active-area centre, both relative to the PCB
    # centre. The FPC leaves one edge of the glass, so neither is centred.
    "disp_glass_dx": 0.0,
    "disp_glass_dy": 0.0,
    "disp_active_dx": 0.0,
    "disp_active_dy": 0.0,
    "window": 29.0,             # 27.6 active + 0.7 reveal per side
    "ribbon_relief": 2.0,       # extra pocket on -Y for the FPC fold
    "plug_wall": 1.7,           # plug thickness behind the lip
    "rib_h": 2.4,               # height of the PCB-locating rib
    "rib_w": 1.7,               # rib wall thickness
    "ribbon_gap": 32.0,         # rib omitted on -Y for the FPC fold, but
                                # corner tabs remain so the module cannot
                                # drop: gravity acts along -Y once installed
    # The 8-pin header is taller than the space behind the module, so it
    # passes through a slot in the flange. MEASURE: which edge it sits on,
    # how far in, and how far it stands off once your wires are soldered.
    "disp_header_w": 20.4,      # 8 pins at 2.54 mm
    "disp_header_t": 2.6,       # body depth, along Y
    "disp_header_h": 8.5,       # rearward: 2.5 body + 6 pin
    "disp_header_cy": 14.3,     # centre, near the PCB's +Y edge

    # ---- uFL antenna, Adafruit 2308 -------------------------------------
    # The uFL board (5348) has NO onboard antenna, so this is mandatory.
    # MEASURE if you use a different antenna: many are 45 x 7 or 21 x 7.
    "ant_w": 40.0,              # flexible FPC antenna, 40 x 8 mm
    "ant_h": 8.0,
    "ant_clear": 0.4,
    "ant_cy": 21.8,             # centred in the strip above the display
    "ant_depth": 1.2,           # pocket depth: antenna + adhesive pad

    # ---- Adafruit QT Py ESP32-S2 with uFL (5348) ------------------------
    "qt_w": 21.8,               # 21.8 x 17.9 x 5.7 mm, no mounting holes
    "qt_h": 17.9,
    "qt_t": 1.6,
    "qt_slot_t": 1.9,           # card-slot height, PCB thickness + play
    "qt_cy": 8.5,               # board centre in the MCU compartment
    # Sat deep enough in the cavity that a USB-C plug clears the flange and
    # can turn towards the rear notch without a violent bend.
    "qt_rail_z": 10.0,          # slot floor, measured from the flange rear
    "qt_rail_h": 14.0,          # rail column height
    "qt_rail_overlap": 0.8,     # how far the groove lips catch the PCB edge
    "qt_usb_w": 9.0,            # USB-C shell, along Y
    "qt_usb_h": 3.2,            # along Z
    "qt_usb_d": 7.5,            # along X, 1.5 of it overhanging the PCB
    "qt_comp_h": 2.6,           # ESP32-S2 module height on the front face

    # ---- Adafruit SHT41 (5776; the 5665 is the SHT45, same outline) ------
    "sht_w": 25.5,              # 25.5 x 17.6 x 4.8 mm
    "sht_h": 17.6,
    "sht_cy": -13.2,            # board centre, inside the sensor duct
    # MEASURE: centre distance of the two mounting holes on the board.
    "sht_hole_pitch": 20.3,
    "sht_boss_od": 4.5,
    "sht_boss_h": 2.6,
    "sht_pilot": 1.7,           # M2 self-tapping pilot
    "sht_pcb_t": 1.0,
    "sht_conn_w": 4.5,          # STEMMA QT socket, along Y
    "sht_conn_h": 3.0,          # along Z
    "sht_conn_d": 6.5,          # along X

    # ---- body ------------------------------------------------------------
    "flange": 71.0,             # square, stays hidden behind the 80.7 frame
    "flange_t": 2.0,
    "box_screw_pitch": 60.0,    # flush-box screw centres, M3.5
    "box_slot_w": 4.0,
    "box_slot_len": 9.0,
    "shell_od": 56.0,           # fits a round 60 mm flush box
    "shell_wall": 1.8,          # bore must clear the SHT41 board's corners
    "cavity_depth": 22.0,       # rearward, from the flange's rear face
    "divider_y": -3.0,          # wall between sensor duct and MCU bay
    "divider_t": 2.0,

    # ---- clamp screws, faceplate <-> body --------------------------------
    # Pushed out in X so the antenna pocket clears the posts; Y is set by the
    # display module, which the posts must miss.
    "clamp_x": 23.8,
    "clamp_y": 23.0,
    "post_od": 5.0,
    "post_tap": 2.05,           # M2.5 self-tapping pilot
    "post_tap_depth": 7.0,
    "clamp_free": 2.9,          # M2.5 free fit through the flange

    # ---- ventilation -----------------------------------------------------
    "vent_slot_w": 1.4,
    "vent_slot_n": 9,
    "vent_slot_pitch": 3.0,
    "vent_y0": -24.0,           # faceplate grille, vertical slots
    "vent_y1": -16.0,
    "grille_ys": (-17.5, -20.0, -22.5),   # flange grille, horizontal slots
    "grille_w": 1.4,

    # ---- wiring ----------------------------------------------------------
    "wire_slot_pad": 3.0,       # clearance round the header, in the flange
    "cable_slot_w": 6.0,        # Qwiic cable, through the divider
    "cable_slot_h": 4.0,
    "usb_notch_w": 8.0,         # rear notch for the USB-C cable
    "usb_notch_d": 8.0,

    "clear": 0.4,               # generic print clearance, per side
}

NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def vi(v):
    return adsk.core.ValueInput.createByReal(v * MM)


def pt(x, y):
    return adsk.core.Point3D.create(x * MM, y * MM, 0.0)


def _extrude(comp, prof, z0, z1, op):
    ext = comp.features.extrudeFeatures
    ei = ext.createInput(prof, op)
    ei.setDistanceExtent(False, vi(z1 - z0))
    if abs(z0) > 1e-9:
        ei.startExtent = adsk.fusion.OffsetStartDefinition.create(vi(z0))
    return ext.add(ei)


def rect(comp, x0, y0, x1, y1, z0, z1, op):
    """Extrude a corner-to-corner rectangle between two z planes."""
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.sketchCurves.sketchLines.addTwoPointRectangle(pt(x0, y0), pt(x1, y1))
    return _extrude(comp, sk.profiles.item(0), z0, z1, op)


def boxc(comp, cx, cy, w, h, z0, z1, op):
    """Same, but centred on (cx, cy)."""
    return rect(comp, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, z0, z1, op)


def circ(comp, cx, cy, dia, z0, z1, op):
    sk = comp.sketches.add(comp.xYConstructionPlane)
    sk.sketchCurves.sketchCircles.addByCenterRadius(pt(cx, cy), dia / 2 * MM)
    return _extrude(comp, sk.profiles.item(0), z0, z1, op)


def slot_x(comp, cx, cy, length, width, z0, z1, op):
    """Round-ended slot along X: a rectangle plus a cap circle at each end."""
    flat = length - width
    boxc(comp, cx, cy, flat, width, z0, z1, op)
    circ(comp, cx - flat / 2, cy, width, z0, z1, op)
    circ(comp, cx + flat / 2, cy, width, z0, z1, op)


def clamp_points(p):
    """The four faceplate/body clamp positions. Shared so they cannot drift."""
    return [(sx * p["clamp_x"], sy * p["clamp_y"])
            for sx in (-1, 1) for sy in (-1, 1)]


def add_component(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


# ---------------------------------------------------------------------------
# faceplate
# ---------------------------------------------------------------------------

def build_faceplate(comp, p):
    ap = p["aperture"]
    plug = ap - 2 * p["aperture_clear"]
    lip = ap + 2 * p["lip_overlap"]
    z_lip = -p["lip_t"]
    z_plug = p["lip_t"] + p["plug_wall"]
    z_frame_rear = p["frame_clamp_t"]

    # lip, proud of the frame's front face, then the plug filling the aperture
    boxc(comp, 0, 0, lip, lip, z_lip, 0.0, NEW)
    boxc(comp, 0, 0, plug, plug, 0.0, z_plug, JOIN)

    # pocket for the panel glass, plus a relief on -Y for the FPC fold. The
    # relief is only as wide as the ribbon gap, so the rib's bottom corner
    # tabs further out still sit on solid material.
    gx, gy = p["disp_glass_dx"], p["disp_glass_dy"]
    gw = p["disp_glass_w"] + 2 * p["clear"]
    gh = p["disp_glass_h"] + 2 * p["clear"]
    z_pocket = z_plug - p["disp_glass_t"]
    boxc(comp, gx, gy, gw, gh, z_pocket, z_plug, CUT)
    rect(comp, -p["ribbon_gap"] / 2, gy - gh / 2 - p["ribbon_relief"],
         p["ribbon_gap"] / 2, gy - gh / 2 + 0.1, z_pocket, z_plug, CUT)

    # viewing window, through the remaining front wall
    z_win = z_plug - p["disp_glass_t"]
    boxc(comp, p["disp_active_dx"], p["disp_active_dy"],
         p["window"], p["window"], z_lip - 0.5, z_win + 0.05, CUT)

    # Rib locating the module PCB: two full long sides plus two bottom corner
    # tabs. The bottom tabs carry the module's weight, so the -Y edge cannot
    # be left fully open even though the FPC folds around it. There is
    # deliberately no rib on +Y: gravity holds the module down onto the bottom
    # tabs, so a top rib locates nothing, and the whole top strip is needed
    # for the antenna pocket.
    pw = p["disp_pcb_w"] + 2 * p["clear"]
    ph = p["disp_pcb_h"] + 2 * p["clear"]
    rw = p["rib_w"]
    z_rib = z_plug + p["rib_h"]
    for sx in (-1, 1):
        rect(comp, sx * (pw / 2), -ph / 2, sx * (pw / 2 + rw), ph / 2,
             z_plug, z_rib, JOIN)
    for sx in (-1, 1):
        rect(comp, sx * p["ribbon_gap"] / 2, -ph / 2,
             sx * (pw / 2 + rw), -ph / 2 - rw, z_plug, z_rib, JOIN)

    # Pocket for the uFL antenna, in the rear face of the top strip. This is
    # the best RF real estate in the build: ~2 mm of plastic to the room,
    # clear of the module's ground plane, and outside the flush box. The
    # cable drops into the flange's wire slot directly below.
    boxc(comp, 0, p["ant_cy"], p["ant_w"] + 2 * p["ant_clear"],
         p["ant_h"] + 2 * p["ant_clear"],
         z_plug - p["ant_depth"], z_plug, CUT)

    # clamp posts, tapped for M2.5 from the rear
    for cx, cy in clamp_points(p):
        circ(comp, cx, cy, p["post_od"], z_plug, z_frame_rear, JOIN)
        circ(comp, cx, cy, p["post_tap"],
             z_frame_rear - p["post_tap_depth"], z_frame_rear + 0.05, CUT)

    # vent grille: vertical slots feeding the sensor duct behind the flange
    n = p["vent_slot_n"]
    for i in range(n):
        cx = (i - (n - 1) / 2) * p["vent_slot_pitch"]
        rect(comp, cx - p["vent_slot_w"] / 2, p["vent_y0"],
             cx + p["vent_slot_w"] / 2, p["vent_y1"],
             z_lip - 0.5, z_plug + 0.5, CUT)


# ---------------------------------------------------------------------------
# body
# ---------------------------------------------------------------------------

def build_body(comp, p):
    z_f0 = p["frame_clamp_t"]                 # flange front == wall surface
    z_f1 = z_f0 + p["flange_t"]
    z_rear = z_f1 + p["cavity_depth"]
    through = (z_f0 - 0.4, z_f1 + 0.4)
    r_in = (p["shell_od"] - 2 * p["shell_wall"]) / 2

    # flange, then the shell reaching into the flush box (open at the back)
    boxc(comp, 0, 0, p["flange"], p["flange"], z_f0, z_f1, NEW)
    circ(comp, 0, 0, p["shell_od"], z_f1, z_rear, JOIN)
    circ(comp, 0, 0, 2 * r_in, z_f1 - 0.05, z_rear + 0.05, CUT)

    # M3.5 slots to the flush box, slotted for a little levelling range
    for sx in (-1, 1):
        slot_x(comp, sx * p["box_screw_pitch"] / 2, 0,
               p["box_slot_len"], p["box_slot_w"], *through, CUT)

    # M2.5 clamp holes, aligned with the faceplate posts. They sit outside
    # the shell, so the screws stay reachable from inside the box.
    for cx, cy in clamp_points(p):
        circ(comp, cx, cy, p["clamp_free"], *through, CUT)

    # grille into the sensor duct, x extent kept inside the shell bore
    for gy in p["grille_ys"]:
        half = (r_in ** 2 - gy ** 2) ** 0.5 - 1.5
        rect(comp, -half, gy - p["grille_w"] / 2,
             half, gy + p["grille_w"] / 2, *through, CUT)

    # Slot for the display's 8-pin header. There is only ~7.3 mm behind the
    # module and the header needs ~8.5, so it passes through the flange and
    # the wires are landed on the MCU side.
    pad = p["wire_slot_pad"]
    boxc(comp, 0, p["disp_header_cy"], p["disp_header_w"] + 2 * pad,
         p["disp_header_t"] + 2 * pad, *through, CUT)

    # standoff pads pressing the module PCB forward against the rib
    for sx in (-1, 1):
        for sy in (-1, 1):
            boxc(comp, sx * 22.0, sy * 14.5, 4.0, 4.0,
                 p["lip_t"] + p["plug_wall"] + p["disp_pcb_t"] + 0.2, z_f0, JOIN)

    # divider: seals the sensor duct off from the MCU bay so the ESP32's
    # self-heating does not reach the SHT41, with a slot for the Qwiic cable
    dy = p["divider_y"]
    dt = p["divider_t"]
    rect(comp, -26.0, dy - dt / 2, 26.0, dy + dt / 2, z_f1, z_rear, JOIN)
    boxc(comp, 0, dy, p["cable_slot_w"], dt + 1.0,
         z_f1 + 5.6, z_f1 + 5.6 + p["cable_slot_h"], CUT)

    # SHT41 bosses, chip facing the grille
    for sx in (-1, 1):
        cx = sx * p["sht_hole_pitch"] / 2
        circ(comp, cx, p["sht_cy"], p["sht_boss_od"],
             z_f1, z_f1 + p["sht_boss_h"], JOIN)
        circ(comp, cx, p["sht_cy"], p["sht_pilot"],
             z_f1, z_f1 + p["sht_boss_h"] + 0.05, CUT)

    # QT Py card slot: two grooved rails plus an end stop, board slides in
    # from +X with its USB-C connector trailing towards the rear notch
    qw, qh = p["qt_w"], p["qt_h"]
    cy = p["qt_cy"]
    rail_x = qw / 2 + 0.6
    z_r0, z_r1 = z_f1, z_f1 + p["qt_rail_h"]
    z_s0 = z_f1 + p["qt_rail_z"]
    z_s1 = z_s0 + p["qt_slot_t"]
    # The rails must reach *inside* the board outline, so that cutting the
    # groove leaves a lip on each side of it. Faced flush with the board edge
    # they would locate nothing: the groove cut would clear them entirely.
    ov = p["qt_rail_overlap"]
    for sy in (-1, 1):
        rect(comp, -rail_x, cy + sy * (qh / 2 - ov),
             rail_x, cy + sy * (qh / 2 + 4.0), z_r0, z_r1, JOIN)
    rect(comp, -rail_x - 3.0, cy - qh / 2 - 4.0, -qw / 2, cy + qh / 2 + 4.0,
         z_r0, z_r1, JOIN)
    rect(comp, -qw / 2, cy - qh / 2 - p["clear"], rail_x + 4.0,
         cy + qh / 2 + p["clear"], z_s0, z_s1, CUT)

    # rear notch so the USB-C cable can leave sideways instead of straight back
    rect(comp, r_in - 1.0, -p["usb_notch_w"] / 2, p["shell_od"] / 2 + 1.0,
         p["usb_notch_w"] / 2, z_rear - p["usb_notch_d"], z_rear + 0.5, CUT)


# ---------------------------------------------------------------------------
# placeholders
#
# Simplified stand-ins for the three boards, placed exactly where the case
# expects them. They are not models of the real parts: only the outline and
# the tall features that can collide are represented. Use them with
# Inspect -> Interference, then hide or delete the components.
# ---------------------------------------------------------------------------

def build_placeholders(root, p):
    z_plug = p["lip_t"] + p["plug_wall"]
    z_f1 = p["frame_clamp_t"] + p["flange_t"]

    # --- display module, glass forward into the faceplate pocket ---------
    comp = add_component(root, "ph_display")
    z_pcb0 = z_plug
    z_pcb1 = z_pcb0 + p["disp_pcb_t"]
    boxc(comp, p["disp_glass_dx"], p["disp_glass_dy"],
         p["disp_glass_w"], p["disp_glass_h"],
         z_plug - p["disp_glass_t"], z_plug, NEW)
    boxc(comp, 0, 0, p["disp_pcb_w"], p["disp_pcb_h"], z_pcb0, z_pcb1, NEW)
    # active area, as a thin witness pad to check window alignment
    boxc(comp, p["disp_active_dx"], p["disp_active_dy"], 27.6, 27.6,
         z_plug - p["disp_glass_t"] - 0.2, z_plug - p["disp_glass_t"], NEW)
    # the 8-pin header, the one feature that has to pass through the flange
    boxc(comp, 0, p["disp_header_cy"], p["disp_header_w"], p["disp_header_t"],
         z_pcb1, z_pcb1 + p["disp_header_h"], NEW)

    # --- uFL antenna, in the faceplate pocket -----------------------------
    comp = add_component(root, "ph_antenna")
    boxc(comp, 0, p["ant_cy"], p["ant_w"], p["ant_h"],
         z_plug - p["ant_depth"], z_plug - p["ant_depth"] + 0.4, NEW)

    # --- QT Py ESP32-S2, in its card slot --------------------------------
    comp = add_component(root, "ph_qtpy")
    z_q0 = z_f1 + p["qt_rail_z"]
    z_q1 = z_q0 + p["qt_t"]
    boxc(comp, 0, p["qt_cy"], p["qt_w"], p["qt_h"], z_q0, z_q1, NEW)
    # Component side faces FORWARD, into the 10 mm gap ahead of the slot. The
    # rear face must stay flat, because that is where the groove lips run.
    boxc(comp, 0, p["qt_cy"], p["qt_w"] - 4.0, p["qt_h"] - 3.0,
         z_q0 - p["qt_comp_h"], z_q0, NEW)
    # USB-C shell, overhanging the +X edge towards the rear notch
    usb_cx = p["qt_w"] / 2 + 1.5 - p["qt_usb_d"] / 2
    boxc(comp, usb_cx, p["qt_cy"], p["qt_usb_d"], p["qt_usb_w"],
         z_q0 - p["qt_usb_h"], z_q0, NEW)

    # --- SHT41, on its bosses in the sensor duct --------------------------
    comp = add_component(root, "ph_sht41")
    z_s0 = z_f1 + p["sht_boss_h"]
    z_s1 = z_s0 + p["sht_pcb_t"]
    boxc(comp, 0, p["sht_cy"], p["sht_w"], p["sht_h"], z_s0, z_s1, NEW)
    for sx in (-1, 1):
        cx = sx * (p["sht_w"] / 2 - p["sht_conn_d"] / 2)
        boxc(comp, cx, p["sht_cy"], p["sht_conn_d"], p["sht_conn_w"],
             z_s1, z_s1 + p["sht_conn_h"], NEW)
    # the sensor die, facing the grille
    boxc(comp, 0, p["sht_cy"], 3.0, 3.0, z_s0 - 1.0, z_s0, NEW)


LOG = os.path.join(os.path.expanduser("~"), "klimacontrol_gira55_log.txt")


def _log(text):
    """Fusion message boxes cannot be copied from, so mirror to a file."""
    try:
        with open(LOG, "w") as fh:
            fh.write(text)
    except Exception:
        pass


def run(context):
    app = adsk.core.Application.get()
    ui = app.userInterface
    step = "startup"
    try:
        step = "new document"
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent

        # The root component's name tracks the document name, and on an
        # unsaved document Fusion rejects the assignment. Not worth failing on.
        try:
            root.name = "KlimaControl Gira 55"
        except Exception:
            pass

        step = "faceplate"
        build_faceplate(add_component(root, "faceplate"), P)
        step = "body"
        build_body(add_component(root, "body"), P)
        if PLACEHOLDERS:
            step = "placeholders"
            build_placeholders(root, P)

        _log("OK\nBuilt faceplate, body and placeholders.\n")
        ui.messageBox(
            "KlimaControl Gira 55 case built.\n\n"
            "Components: faceplate, body"
            + (", ph_display, ph_qtpy, ph_sht41" if PLACEHOLDERS else "")
            + ".\nCheck the MEASURE values in cad/fusion/README.md "
            "before printing."
        )
    except Exception:
        report = "FAILED while building: {}\n\n{}".format(
            step, traceback.format_exc())
        _log(report)
        if ui:
            ui.messageBox(report + "\nAlso written to:\n" + LOG)
