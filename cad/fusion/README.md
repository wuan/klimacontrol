# KlimaControl — Gira System 55 case

A two-part 3D-printable case that puts the KlimaControl hardware into a
standard German flush-mount box behind a **Gira System 55** cover frame.

`klimacontrol_gira55.py` is an Autodesk Fusion script. It builds both parts as
solids in a new design; it is the source of truth for the geometry.

```
Utilities → Add-Ins → Scripts and Add-Ins → Scripts → "+" (Create)
→ pick this .py, then Run
```

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
§4.

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
carries a thin witness pad at the active area, so you can see immediately
whether the faceplate window is centred on the image.

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

## 4. Assembly order

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

## 5. MEASURE before you print

These are the values in `P` I could not verify from a data sheet. The rest come
from published outlines and are safe.

| Key | Default | What to measure |
|---|---|---|
| `frame_clamp_t` | 11.4 | **The critical one.** Front-to-rear thickness of your frame at the aperture. The data sheet's 11.4 mm is the frame's *overall* depth; if it has a rear collar that enters the box, the clamping thickness is less and the clamp will not close. |
| `disp_glass_dx/dy` | 0.0 | Glass centre relative to the module PCB centre. The FPC leaves one edge, so it is not centred. |
| `disp_active_dx/dy` | 0.0 | Active-area centre relative to the PCB centre. Drives where the window lands — get this wrong and the image is off-centre in the bezel. |
| `disp_header_cy` | 14.3 | Which edge of the module the 8-pin header sits on and how far in. Drives where the flange's wire slot lands — if this is wrong the header hits solid plastic. |
| `disp_header_h` | 8.5 | How far the header stands off the PCB *with your wires soldered on*. If it exceeds ~10 mm it will poke out the back of the flange. |
| `ant_w` / `ant_h` | 40.0 / 8.0 | Only if you use something other than the 2308. 45 × 7 and 21 × 7 are both common; 45 mm will **not** fit between the clamp posts, which leave 42.6 mm of clear width. |
| `sht_hole_pitch` | 20.3 | Centre distance of the two mounting holes on the breakout. |

Print the faceplate alone first as a fit check: it is a 20-minute part and it
validates the aperture fit, the clamp gap, and the window alignment at once.

## 6. Known limitations

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
