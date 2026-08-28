# E-Paper Display Wiring

How to connect a Waveshare 1.54" e-paper module to the Adafruit QT Py ESP32-S2.

This is the hardware-side companion to `src/display/DisplayPins.h`, which is the
source of truth for the pin numbers. If you change a pin there, change the table
here in the same commit.

The display is **disabled by default**. Wire it up, then enable it under
*Settings → E-Paper Display* in the web interface. The device restarts to apply
the change.

---

## 1. Which module

The firmware supports exactly one panel:

> **Waveshare 1.54inch e-Paper Module** — monochrome, SSD1681 controller,
> 200×200 pixels, with the 8-pin 2.54 mm header.

Waveshare sells several things under a similar name. Check before you buy:

| Product | Supported | Why |
|---|---|---|
| 1.54inch e-Paper Module (V2, mono) | ✅ | This is the one. Partial refresh, ~2.6 s full refresh. |
| 1.54inch e-Paper Module **(B)** or **(C)** | ❌ | Three-colour (black/white/red or /yellow). No partial refresh at all, and a full refresh takes ~15 s of flashing. A live climate readout is not usable on these. |
| 1.54inch e-Paper **(D)** | ❌ | Flexible panel, different controller. |
| Bare panel + **e-Paper Driver HAT** | ⚠️ | Works, but see the jumper note in §4. |

The supported module has a **24-pin FPC ribbon** from the glass into a small
driver PCB, and an **8-pin male header** on the other edge labelled
`VCC GND DIN CLK CS DC RST BUSY`. If your board instead has a 40-pin Raspberry Pi
header, you have the Driver HAT variant.

---

## 2. Wiring

```
   Waveshare 1.54" V2                        QT Py ESP32-S2
   8-pin header                              (pad silkscreen labels)
   ┌───────────────┐
   │ VCC    red    ├──────────────────────────► 3V
   │ GND    brown  ├──────────────────────────► GND
   │ DIN    blue   ├──────────────────────────► MO      (GPIO35)  ─┐ hardware
   │ CLK    yellow ├──────────────────────────► SCK     (GPIO36)  ─┘ SPI
   │ CS     orange ├──────────────────────────► A0      (GPIO18)  ─┐
   │ DC     white  ├──────────────────────────► A3      (GPIO8)    │ control
   │ RST    purple ├──────────────────────────► A2      (GPIO9)    │ lines
   │ BUSY   grey   ├──────────────────────────► A1      (GPIO17)  ─┘
   └───────────────┘

                              MI  (GPIO37) ─── leave unconnected (see §3)
```

| Panel pin | Ribbon colour | QT Py pad | GPIO | Direction |
|---|---|---|---|---|
| `VCC` | red | `3V` | — | power, 3.3 V |
| `GND` | brown | `GND` | — | ground |
| `DIN` | blue | `MO` | 35 | MCU → panel (SPI MOSI) |
| `CLK` | yellow | `SCK` | 36 | MCU → panel (SPI clock) |
| `CS` | orange | `A0` | 18 | MCU → panel, active LOW |
| `DC` | white | `A3` | 8 | MCU → panel, data/command select |
| `RST` | purple | `A2` | 9 | MCU → panel, active LOW |
| `BUSY` | grey | `A1` | 17 | panel → MCU, active HIGH |

> **Verify the colours against your own cable.** The scheme above is Waveshare's
> usual one, but it is a convention, not a guarantee. The silkscreen labels on
> the module are authoritative.

Pads left free after wiring: `SDA`, `SCL`, `TX`, `RX`, `MI`. The primary `Wire`
bus (`SDA`/`SCL`) is untouched, as is the STEMMA QT connector
(`SDA1`/`SCL1` = GPIO41/40) that the sensors use.

---

## 3. Why `CS` is not on the `MI` pad

The panel is write-only, so the `MI` (MISO) pad carries no signal and looks like
a free pin. It cannot be used for chip select.

`GxEPD2_EPD::init()` sets the CS pin to `OUTPUT` and *then* calls
`SPI.begin()` (`GxEPD2_EPD.cpp:59-66`). On ESP32-S2, `SPI.begin()` always
attaches MISO — a negative pin argument is silently rewritten to GPIO37 — and
runs `pinMode(37, INPUT)` (`esp32-hal-spi.c:203-232`). A chip select on GPIO37
would therefore be reconfigured back to an input immediately after being set up,
and the panel would never respond. There is no way to suppress the MISO attach
through the Arduino SPI API.

The firmware leaves GPIO37 electrically unconnected. SPI claiming it as an
unread input is harmless.

---

## 4. Power and decoupling

- **3.3 V only.** Both the supply *and* the data lines are 3.3 V. Do not feed
  the module 5 V from the `5V` pad.
- The panel draws a few milliamps idle and peaks around **25 mA** for the ~2.6 s
  of a full refresh, driven by its on-board charge pump. Between refreshes it
  draws essentially nothing, and e-paper keeps its image with no power at all.
- **Add a 100 µF bulk capacitor across the module's `VCC`/`GND`.** This board
  has a history of `ESP_RST_BROWNOUT` resets from WiFi TX inrush (see
  `resetReasonStr()` in `src/main.cpp`); a refresh transient landing on top of a
  WiFi burst stacks two current spikes on the same rail. The capacitor is cheap
  insurance.

**Driver HAT variant only:** if you are using a bare panel with Waveshare's
e-Paper Driver HAT carrier, set its two jumpers before applying power:

- voltage jumper → **3.3 V** (not 5 V)
- interface jumper → **4-wire SPI** (not 3-wire)

---

## 5. Before applying power

A reversed `VCC`/`GND` will destroy the panel. Check with a multimeter first:

- [ ] Continuity from each QT Py pad to the intended header pin — all eight.
- [ ] No short between `3V` and `GND` (measure resistance; it should not be near
      zero).
- [ ] `VCC` goes to `3V` and `GND` goes to `GND`. Confirm twice; the two wires
      are adjacent on the header and easy to swap.
- [ ] No wire on the `MI` pad.
- [ ] The 24-pin FPC ribbon is fully seated in its connector and the latch is
      closed. A partially seated ribbon typically gives a blank or streaky panel.

---

## 6. Verifying it works

1. Flash the firmware and open the USB CDC serial console.
2. Enable the display: *Settings → E-Paper Display → Enable Display → Save*.
   The device restarts.
3. Expected sequence on the panel:
   - a **splash** with "KlimaControl", the device name, and "starting..."
   - then the first temperature/humidity reading, as a full refresh
   - subsequent updates are partial refreshes (no black/white flash), at most one
     per the configured interval (default 60 s)
   - every 12th partial is promoted to a full refresh to clear ghosting
4. Expected log lines:

```
I display: E-paper display initialised (rotation=0, page buffer=625 B)
I display: Splash shown
I display: Display enabled (rotation=0, min interval=60 s)
```

If no sensor is attached, the panel shows `--.-°` and `-- %rH` — that is the
placeholder, not a fault.

---

## 7. Troubleshooting

| Symptom | Likely cause | Check |
|---|---|---|
| Panel completely blank, no splash | Not powered, or `CS`/`DC` swapped | §5 continuity check; confirm `CS`→`A0` and `DC`→`A3` |
| Panel blank, `Display faulted` in the log | `BUSY` not connected, or floating | `BUSY` must go to `A1` (GPIO17). A floating input reads HIGH forever, so the driver waits out its timeout on every refresh |
| Panel blank, no log output at all | Display not enabled in config | *Settings → E-Paper Display*; the default is off |
| Random noise / streaks | `DIN` and `CLK` swapped | `DIN`→`MO` (GPIO35), `CLK`→`SCK` (GPIO36) |
| Image appears then fades or is very faint | Wrong panel variant (tri-colour module) | See §1 — B/C modules are not supported |
| Faint shadow of the previous reading | Normal ghosting between full refreshes | Clears on the next full refresh; lower the refresh interval or check that the panel is a V2 |
| Text upside-down or sideways | Rotation setting | *Settings → E-Paper Display → Rotation* |
| Device reboots during a refresh | Brownout | `Reset reason: BROWNOUT` in the boot log — add the 100 µF cap from §4 |
| Panel keeps showing an old reading after disabling | Blanking failed | It should be cleared during the save, before the restart. Check the console for `Display disabled and blanked`; if absent, the panel had already faulted (see below) and could not be driven |

### The fault guard

If three consecutive refreshes each take longer than 12 s — which is what a
disconnected or stuck `BUSY` line produces — the firmware logs:

```
E display: Display faulted after 3 consecutive timeouts - check the BUSY line
           and the panel connection. No further refreshes until restart.
```

and stops trying. WiFi, MQTT and sensor reads keep running normally. The
persisted configuration is deliberately **not** changed, so the web UI still
shows the display as enabled; reseat the connector and restart to re-test.

---

## 8. Pin reference

For completeness, the pins this board uses for other purposes — do not reuse
them for the display:

| GPIO | Used by |
|---|---|
| 41 / 40 | STEMMA QT I2C (`SDA1`/`SCL1`) — sensors |
| 39 | NeoPixel data (status LED) |
| 38 | NeoPixel power |
| 37 | claimed by `SPI.begin()` as MISO (see §3) |
| 27–32 | SPI flash and PSRAM (not broken out) |
| 19 / 20 | native USB (not broken out) |
| 0 | BOOT button |
