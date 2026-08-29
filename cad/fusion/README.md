# KlimaControl — Gira System 55 case

A two-part 3D-printable case that puts the KlimaControl hardware into a
standard German flush-mount box behind a **Gira System 55** cover frame.

> **Being rebuilt step by step.** `klimacontrol.py` + the `kc/` package is the
> new, incremental model — currently **step 1: the viewport only** (lip, plug,
> glass pocket, FPC relief, window). Later steps add the locating rib, antenna
> pocket, clamp posts and vent grille, then the body.
> `klimacontrol_old.py` below is the earlier one-shot script; it stays as
> reference until the package supersedes it. Where the two disagree the package
> is right — notably it carries the measured 3 mm active-area offset, which the
> old script has as `0.0`.
>
> See [Installing into Fusion](#installing-into-fusion),
> [§4a Step 1: the viewport](#4a-step-1-the-viewport) and
> [§4b Layout of the model](#4b-layout-of-the-model).

## Installing into Fusion

```
./cad/fusion/install.sh          # link it   (--remove to unlink)
```

Then in Fusion: **Utilities → Add-Ins → Scripts and Add-Ins** (`Shift+S`) →
**My Scripts → klimacontrol → Run**. Restart Fusion, or just reopen that
dialog, if the entry does not appear straight away.

Fusion discovers a script as a *folder* under its `API/Scripts` directory
containing `<folder>.py` plus `<folder>.manifest` — a loose `.py` is not
enough, and neither is a folder whose name doesn't match the script. So
`install.sh` symlinks `cad/fusion` in under the name `klimacontrol`, which
makes Fusion see:

```
API/Scripts/klimacontrol/     → symlink to cad/fusion
    klimacontrol.py           the entry point Fusion runs
    klimacontrol.manifest     what makes Fusion list it at all
    kc/                       the package the entry point imports
```

Nothing is copied, so **the repo is the live source** — edit a file, hit Run
again, and the change is in the model. Two details make that work:

* `HERE` in the entry point is `os.path.realpath(__file__)`, so it resolves
  *through* the symlink to the repo, and `kc` is imported from exactly one
  path rather than two.
* `_load()` purges `sys.modules` first, so Fusion's session-long interpreter
  cannot serve you a stale copy of an edited module.

On Windows the same script works from Git Bash; the target is
`%APPDATA%\Autodesk\Autodesk Fusion 360\API\Scripts`.

`klimacontrol.manifest` is copied from the template this Fusion build ships
(`Autodesk Fusion.app/Contents/Libraries/Neutron/Python/Default/`), which uses
`"autodeskProduct": "Fusion"` — worth knowing, because older examples on the
web say `"Fusion360"`.

---

`klimacontrol_old.py` is the earlier one-shot Fusion script — it builds both
parts as solids in a new design. Sections 1–3 and 5–7 below still describe it
and are the design intent for the steps not yet rebuilt.

It is **not registered with Fusion**: `install.sh` links the folder in as
`klimacontrol`, so Fusion picks up `klimacontrol.py` and ignores this one. To
run it anyway, rename it to match a folder of its own, or temporarily point
`install.sh`'s `NAME` at it.

The script does **not** create Fusion user parameters — the model is not
editable from the Parameters dialog. To change a dimension, edit the `P` dict
at the top of the script and re-run it. That is the whole parametric loop.

---

## 1. What it holds

| Part | Product | Outline |
|---|---|---|
| Display | Waveshare 1.54" e-Paper Module (V2, SSD1681) | 48 × 33 mm module, 37.4 × 31.8 mm glass, 27.6 mm active |
| MCU | Adafruit QT Py ESP32-S2 **with uFL** (5348) | 21.8 × 17.9 × 5.7 mm, no mounting holes |
| Antenna | Adafruit 2.4 GHz flexible uFL antenna (2308) | 40 × 8 mm, 100 mm pigtail |
| Sensor | Adafruit SHT41 (**5776**) | 25.5 × 17.6 × 4.8 mm, STEMMA QT |
| Frame | Gira Standard 55 cover frame (021103) | 80.7 × 80.7 × 11.4 mm, 55 × 55 aperture |

> You mentioned Adafruit **5665** — that part number is the **SHT45**, not the
> SHT41 (which is 5776). Both sit on the identical STEMMA QT breakout outline,
> so either one drops into this case unchanged; only the firmware's sensor
> driver cares.

The uFL board (5348) has **no onboard antenna whatsoever** — the external
antenna is not an upgrade, it is required for the board to reach a network at
all. Nothing else about it differs from the 5325: same outline, same pads.

Wiring is unchanged from [`docs/EINK_DISPLAY_WIRING.md`](../../docs/EINK_DISPLAY_WIRING.md):
8 flying leads from the display to the QT Py pads, and the sensor on the
STEMMA QT connector.

### Antenna placement

The antenna lives in a 40.8 × 8.8 × 1.2 mm pocket in the **rear face of the
faceplate's top strip**, directly above the display. That is the best RF
position the build offers, for three reasons: only ~2.1 mm of plastic between
it and the room; clear of the display module's ground plane, which sits below
and behind it and acts as a mild reflector forward; and entirely outside the
flush box, so a metal box (rare in DE, but they exist) would sit behind the
antenna rather than around it. The pigtail drops straight down into the
flange's wire slot to reach the QT Py.

Making room for it cost one feature: there is **no rib along the module's top
edge** any more. That rib located nothing — gravity holds the module down onto
the two bottom corner tabs — so the whole top strip was free real estate.

Coil the spare pigtail loosely in the cavity above the display. Do not kink it;
1.13 mm coax wants a bend radius of about 5 mm.

## 2. The two parts

**`faceplate`** — plugs into the 55 × 55 aperture. A 0.8 mm lip stands proud of
the frame's front face and overlaps it by 1 mm all round. Behind it: a 1.4 mm
pocket for the panel glass (opened on the −Y edge so the FPC can fold around
the PCB), a 29 mm viewing window, a three-sided rib that locates the module PCB
with a gap for the 8-pin header, four M2.5 clamp posts at (±23, ±23), and a
nine-slot vent grille along the bottom edge.

**`body`** — a 71 × 71 × 2 mm flange (hidden behind the 80.7 mm frame) with two
slotted M3.5 holes on 60 mm centres for the flush box, plus a Ø56 mm shell
reaching 22 mm into the box, open at the rear. Inside: an SHT41 boss pair, a
QT Py slide-in card slot, and a divider wall splitting the cavity into a
**sensor duct** (bottom) and an **MCU bay** (top).

### How the frame is held

The frame is clamped, not clipped — no Gira latch geometry is reverse-engineered.

```
   z=0 ──┐ faceplate lip (0.8 mm, proud)
         │  ┌──────────────┐
         │  │ Gira frame   │  ← sandwiched
   z=11.4│  └──────────────┘
         └─ body flange, pulled forward by 4 × M2.5 into the faceplate posts
```

Four M2.5 screws pass through the flange from inside the box and thread into
the faceplate posts. The posts butt against the flange, so the clamp gap equals
`frame_clamp_t` exactly — **this is the one dimension that must be right**, see
§6.

### Sensor placement and thermal isolation

The SHT41 sits in the bottom duct, its chip facing three horizontal grille
slots in the flange, which line up behind the nine vertical slots in the
faceplate. The divider wall plus the solid flange keep that duct separated from
the QT Py's air, because ESP32-S2 self-heating is a much larger error than a
slow response.

Two consequences worth knowing before you build:

- Air exchange is **bottom-vent only** — there is no chimney, because a
  top exhaust would have to cross the MCU bay. Expect a diffusion-limited
  response on the order of a minute, not seconds. Fine for room climate.
- The display's wire slot at the top of the flange does connect the MCU bay to
  the cavity behind the faceplate. Seal it with a dab of hot glue after wiring
  if you want the isolation to be real.

Even so, a wall box runs warm. Plan on measuring the residual offset against a
reference thermometer and correcting it in the firmware.

## 3. Component placeholders

With `PLACEHOLDERS = True` (the default) the script also builds three
stand-ins, positioned exactly where the case expects each board:
`ph_display`, `ph_antenna`, `ph_qtpy`, `ph_sht41`.

They are not models of the real parts. Each is the board outline plus only
the tall features that can collide — the display's 8-pin header, the QT Py's
USB-C shell and module, the SHT41's two STEMMA QT sockets. `ph_display` also
carries a thin witness pad at the active area. Once `disp_active_dx/dy` are
measured the pad lands on the origin by construction (§4), so it reads as a
direct check on the window: concentric is right, anything else is a mistake in
`window`, not in the module placement.

To check the fit: **Inspect → Interference**, select all six components,
tick *Compute Interlocking Volumes*, Compute. Then hide the placeholders (or
set `PLACEHOLDERS = False` and re-run) before exporting anything for print.

Two things the placeholders already caught, both now fixed in the script:

- **The 8-pin header does not fit behind the module.** There is 7.3 mm
  between the module's rear face and the flange, and a 2.54 mm header with
  wires soldered on stands about 8.5 mm proud. The flange's wire slot is now
  derived from the header's position and size, so the header passes *through*
  the flange and the wires are landed on the MCU side.
- **The QT Py's component side must face forward.** The card slot's groove
  lips run along the board's rear face, so the ESP32-S2 module and the USB-C
  shell go in the 10 mm of clear space ahead of the slot. Fitted the other
  way round, the lips foul the module.

## 4. Centring the image

The Waveshare module's active area is not centred on its 48 × 33 mm PCB — the
FPC leaves one edge of the glass, so the image sits off to one side. There are
two ways to absorb that, and the script deliberately picks one:

- **Move the window** to wherever the image happens to land. Cheap, but the
  window is the only part of this build the room ever looks at, and an
  off-centre hole in a Gira bezel is visible from across the room forever.
- **Move the module** and leave the window square on the aperture. This is
  what the script does.

So `disp_active_dx/dy` describes the *part*, not the *case*: it is the active
area's centre relative to the PCB centre. `disp_shift()` negates it to get the
PCB's position, and everything that locates the module — glass pocket, FPC
relief, rib, standoff pads, the flange's header slot, and the `ph_display`
placeholder — is placed through it. The window and the antenna pocket are the
only display-area features that stay on the origin.

**Sign.** Model `+X` appears to the **left** when you look at the faceplate
from the front, because `+Z` runs rearward into the wall. An image that looks
shifted to the right in the bezel is at *negative* `disp_active_dx`, and the
module is carried towards `+X` to compensate.

**How to measure it.** With the module face up, caliper from the PCB's left
edge to the left edge of the active area, and from the right edge to the right
edge. Half the difference is the offset; give it the sign above. Repeat
vertically. Then re-run the script and confirm the `ph_display` witness pad
sits concentric with the window — after the shift it should land exactly on
the origin, so any visible mismatch is a mistake in `window`, not in the
placement.

**Headroom is tight in both axes**, for different reasons:

- **X, about ±1 mm.** The rib's outer face already sits 26.1 mm from the
  centre against a 27.1 mm plug half-width. Past that the rib breaks out
  through the aperture wall. Buy room by dropping `rib_w` on the crowded
  side, or by shortening that rib, rather than letting the module hang out
  of the plug.
- **Y, about ±1.9 mm.** The four clamp posts do *not* move with the module —
  they are case features, and they stand in the same Z band as the PCB and
  the rib. At `clamp_x = 23.8` they already overlap the PCB's X extent, so
  the only thing keeping them apart is the Y gap, and the shift spends it.
  Buy room by pushing `clamp_y` out from 23.0.

The script checks both on every run, along with the window staying inside the
glass pocket, and appends any warning to the completion dialog and to
`~/klimacontrol_gira55_log.txt`.

## 4a. Step 1: the viewport

Run **`klimacontrol`** ([installed as above](#installing-into-fusion)). It
builds one component, `faceplate`, plus a `ph_display` stand-in when
`PLACEHOLDERS = True`, and reports where everything landed in the completion
dialog and in `~/klimacontrol_cad_log.txt`.

### The measurement

The active area sits **3 mm away from the FPC/header end**, along the module's
**long (48 mm) side**. The PCB and the glass are centred on each other; the
image is not centred on either.

### Why the window is 0.5 mm off centre

The full 3 mm cannot be absorbed by moving the module. The plug that enters the
aperture is 55 − 2 × 0.4 = **54.2 mm**, the PCB with clearance is **48.8 mm**,
so there is only **2.7 mm** of travel before the PCB breaks out through the
aperture wall — and that is with *zero* material left on the crowded side.

`module_carry` splits the offset:

| | value | consequence |
|---|---|---|
| absorbed by moving the module | 2.5 mm | 0.20 mm of plug left on the crowded side — **no locating rib possible there** |
| left for the window | 0.5 mm | bezel border 13.5 mm on the FPC side, 12.5 mm on the image side |

With the FPC end on the **left** as seen from the front, the image sits to the
right of module centre, the module is carried 2.5 mm to the left, and the
window ends up 0.5 mm right of the aperture centre. The script prints all of
this on every run.

The 0.20 mm figure is a warning, not an error — it is the constraint the next
step has to design around. The rib will have to be one-sided (roomy side plus
top and bottom corner tabs), or `module_carry` has to come down and the window
move further off centre. Change one number to explore it:

```python
"module_carry": 2.5,   # 0.0 = module centred, window 3 mm off
```

### Geometry it builds

* Lip 0.8 mm thick, 57 × 57, proud of the frame's front face.
* Plug 54.2 × 54.2, from the frame's front face to z = 2.20.
* Glass pocket 38.2 × 32.6 × 1.4, cut into the plug's rear face.
* FPC relief, 2 mm further out on the header end, 26 mm wide.
* Viewport 29 × 29, cut through the remaining **1.6 mm** front wall,
  concentric with the active area by construction (`window_dx` is derived,
  never typed).

### Still MEASURE

| Key | Default | What to measure |
|---|---|---|
| `disp_glass_dx/dy` | 0.0 | Glass centre relative to the PCB centre. Only sizes the pocket; does not move the window. |
| `active_offset_y` | 0.0 | The same 3 mm measurement on the module's **short** side. Y has 10.2 mm of module travel, so anything plausible fits. |
| `front_wall` | 1.6 | Taste, not a measurement — it sets how deep the window reveal looks. |

## 4b. Layout of the model

One entry point, one module per component.

```
cad/fusion/
  klimacontrol.py       ← the only file you register as a Fusion script
  kc/
    params.py           the P dict. The only place a dimension is typed.
    layout.py           positions derived from P, plus the fit checks
    primitives.py       the sketch-and-extrude layer over the Fusion API
    faceplate.py        component: the front plate
    placeholders.py     component: board stand-ins
```

**Adding a component.** Write `kc/<name>.py` with a `build(comp, p, g)`, then
add one line to `COMPONENTS` in `klimacontrol.py`:

```python
components = [
    ("faceplate", kc["faceplate"].build),
    ("body",      kc["body"].build),        # ← the whole registration
]
```

`p` is the params dict, `g` the derived geometry. A builder receives an empty
Fusion component and puts bodies in it; it never creates the component, never
reads a raw dimension that isn't in `p`, and never touches the Fusion API
except through `kc.primitives`.

**Checking the arithmetic without Fusion.** `kc/params.py` and `kc/layout.py`
import nothing from Fusion, so the numbers can be checked from a terminal:

```
$ python3 cad/fusion/kc/layout.py
module PCB centre  x = +2.50  y = -0.00  (model)
viewport centre    x = -0.50  y = +0.00  (model)
...
```

This prints exactly the report the Fusion dialog shows. Use it when changing
`module_carry`, `active_offset` or `fpc_side` — the fit consequences show up
here in a second instead of after a rebuild.

**Why `_load()` exists.** Fusion keeps one Python interpreter alive for a whole
session, so a plain `import kc.faceplate` on the second run returns the module
object from the first and **silently ignores every edit you just made**. The
entry point drops the package from `sys.modules` before importing, which is
what makes the edit-and-re-run loop work without restarting Fusion. Do not
"simplify" it into a top-level import.

## 5. Assembly order

1. Print both parts. Tap the four faceplate posts M2.5 and the two sensor
   bosses M2 (or run the screws straight in — the pilots are sized for
   self-tapping into PLA/PETG).
2. Solder the 8 display leads and fit the 100 µF bulk cap called for in
   `docs/EINK_DISPLAY_WIRING.md` §4.
3. Peel-and-stick the antenna into the faceplate's top pocket and lay its
   pigtail towards the middle of the plate. Do the antenna **before** the
   display module — the module covers the route and the uFL click is fiddly
   enough without working blind.
4. Drop the display module glass-first into the faceplate pocket. The rib holds
   it laterally and the two bottom corner tabs carry its weight; the four
   standoff pads on the flange press it forward on assembly.
5. Screw the SHT41 to its bosses, chip facing forward. Slide the QT Py into its
   card slot from the +X side, **component side facing forward**, USB-C
   trailing to the rear notch. Connect the Qwiic cable through the divider
   slot.
6. Feed the antenna pigtail and the display's 8-pin header through the flange's
   wire slot. The header is taller than the space behind the module, so it
   deliberately protrudes into the MCU bay — land the display leads and click
   the uFL connector on there.
7. Put the Gira frame over the faceplate, offer up the body, and pull the four
   M2.5 clamp screws up from the rear.
8. Screw the flange to the box with two M3.5 (the slots give a few mm of
   levelling range), USB-C cable exiting through the rear notch.

### Hardware

| Qty | Item |
|---|---|
| 4 | M2.5 × 16 pan head — faceplate clamp |
| 2 | M3.5 × 25 — flush box (usually supplied with the box) |
| 2 | M2 × 6 — SHT41 |
| 1 | 100 µF electrolytic — display bulk decoupling |
| 1 | USB-C cable, right-angle preferred |
| 1 | Adafruit 2308 uFL antenna (or any 40 × 8 mm flexible uFL type) |

### Print notes

Print both parts face-down on the bed (faceplate lip down, flange down): every
overhang is then either a bridge or supported. 0.15 mm layers on the faceplate
for a clean window edge, 0.2 mm is plenty for the body. No supports needed. PETG
if the box shares a wall with anything warm.

## 6. MEASURE before you print

These are the values in `P` I could not verify from a data sheet. The rest come
from published outlines and are safe.

| Key | Default | What to measure |
|---|---|---|
| `frame_clamp_t` | 11.4 | **The critical one.** Front-to-rear thickness of your frame at the aperture. The data sheet's 11.4 mm is the frame's *overall* depth; if it has a rear collar that enters the box, the clamping thickness is less and the clamp will not close. |
| `disp_glass_dx/dy` | — | Glass centre relative to the module PCB centre. The FPC leaves one edge, so it is not centred. |
| `disp_active_dx/dy` | 0.0 | Active-area centre relative to the PCB centre. Drives how far the **module** is carried off the aperture centre — get this wrong and the image is off-centre in the bezel. See [Centring the image](#4-centring-the-image). |
| `disp_header_cy` | 14.3 | Which edge of the module the 8-pin header sits on and how far in. Drives where the flange's wire slot lands — if this is wrong the header hits solid plastic. |
| `disp_header_h` | 8.5 | How far the header stands off the PCB *with your wires soldered on*. If it exceeds ~10 mm it will poke out the back of the flange. |
| `ant_w` / `ant_h` | 40.0 / 8.0 | Only if you use something other than the 2308. 45 × 7 and 21 × 7 are both common; 45 mm will **not** fit between the clamp posts, which leave 42.6 mm of clear width. |
| `sht_hole_pitch` | 20.3 | Centre distance of the two mounting holes on the breakout. |

Print the faceplate alone first as a fit check: it is a 20-minute part and it
validates the aperture fit, the clamp gap, and the window alignment at once.

## 7. Known limitations

- No fillets or chamfers. Every edge is square, so the script never fails on a
  fillet that cannot be computed. Add them in Fusion afterwards if you want
  them — the lip's outer edge is the one that shows.
- BOOT and RESET on the QT Py, and the NeoPixel, are not accessible once
  assembled. Reflashing is over OTA or by pulling the body.
- Dimensioned for **Standard 55**. Other Gira lines share the 55 × 55 aperture
  but differ in outer size and aperture depth: check `frame_clamp_t` and that
  `flange` (71 mm) still hides behind your frame.
- No IP rating, no mains inside. Power is USB-C from the rear only.

## Sources

- [Gira Standard 55 cover frame data sheet (021103)](https://katalog.gira.de/en-INT/datenblatt/021103) — 80.7 × 80.7 × 11.4 mm
- [Gira System 55](https://www.gira.com/en/en/products/systems/gira-system-55) — 55 × 55 mm aperture
- [Waveshare 1.54inch e-Paper Module](https://www.waveshare.com/1.54inch-e-paper-module.htm) — 48 × 33 mm outline, 27.6 × 27.6 mm active
- [Waveshare 1.54inch e-paper V2 datasheet](https://files.waveshare.com/upload/e/e5/1.54inch_e-paper_V2_Datasheet.pdf) — panel outline
- [Adafruit QT Py ESP32-S2 with uFL (5348)](https://www.adafruit.com/product/5348) — 21.8 × 17.9 × 5.7 mm, no onboard antenna
- [Adafruit 2.4 GHz mini flexible WiFi antenna, uFL (2308)](https://www.adafruit.com/product/2308) — 40 × 8 mm, 100 mm cable
- [Adafruit SHT41 (5776)](https://www.adafruit.com/product/5776) — 25.5 × 17.6 × 4.8 mm
