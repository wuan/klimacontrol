## Context

Current layout (rotation 0, panel 200x200):

```
 y=0  +------------------------------------+
      |  blank                             |  <- outside the partial window
 y=30 +- - - REFRESH_WINDOW_Y - - - - - - -+
      |                                    |
      |             21.4 (o)               |  FreeSansBold24pt7b, baseline 85
      |                                    |
      |             43 %rH                 |  FreeSans12pt7b,     baseline 128
      |  ---------------------------------  |  rule                y=152
      |  living-room            22.0 (o)   |  FreeSans9pt7b,      y=170
      |  2026-09-02 14:07   [###..]  (*)   |  FreeSans9pt7b,      y=192
y=199 +------------------------------------+
```

The strip `y 0..29` is excluded from `setPartialWindow(0, 30, 200, 170)`.
GxEPD2 clips drawing outside the active window, and the panel physically retains
the pixels it is not told to rewrite, so anything drawn there survives untouched
across partial refreshes and is only rewritten by a `Full` refresh.

Three facts make that strip the right place for a version string, and the wrong
place for anything else the firmware currently knows:

1. `FIRMWARE_VERSION` is a `#define` from the git tag (`OTAConfig.h:36`, injected
   by `scripts/get_version.py`). It cannot change while the firmware runs.
2. The first paint after boot is unconditionally `Full` (`RefreshPolicy.cpp:105`),
   so a version change — which requires a reflash or an OTA reboot — is always
   followed by a full repaint of the strip.
3. Every other displayed field is runtime-mutable, including the device name,
   which the web UI can change without a reboot. That is why the device name
   stays in footer line 1 rather than moving up.

## Goals / Non-Goals

**Goals**

- Brand mark top-left, firmware version top-right, in the existing blank strip.
- Zero added cost on the hot path: partial refreshes are the common case and must
  not gain work or pixels.
- No new font linked; no new heap or BSS.
- A long developer version degrades legibly instead of overrunning the title.

**Non-Goals**

- Build date, git hash, uptime or IP address on the panel.
- Making the header configurable, or the band's content user-selectable.
- Moving the partial window boundary, or changing what the value/footer zones
  contain.
- A horizontal rule under the band (explicitly declined).

## Decisions

### D1. The band lives outside the partial-refresh window

Drawn inside `runPagedDraw` at `y < 30`, which GxEPD2 clips away when the
partial window is active. The header therefore costs nothing on a partial
refresh and is repainted on every full one.

The alternative — widening the window to `(0, 0, 200, 200)` so the band is always
repainted — was rejected. It would make partial refreshes ~18 % larger for
content that is provably constant, and would rewrite identical pixels forever.

The staleness hazard that forced the footer inside the window does not apply
here, for the reasons in *Context*. This is the load-bearing assumption of the
whole design, so it is stated as a spec requirement rather than left as a
comment: **if a future field in the band ever becomes runtime-mutable, the band
must move inside the window.**

### D2. Both fields in the built-in 5x7 font, hard against the top edge

The band is chrome, not content. Both fields therefore use the built-in 5x7 GFX
font at 1:1 — the same face as the splash's "starting..." line, so it is a known
quantity on this panel — and the whole row sits at a glyph top of **y=4**,
tucked against the panel's top edge rather than floating in the middle of the
30-pixel strip.

An earlier revision set the title in `FreeSans9pt7b` for a size hierarchy
between brand mark and version. Rejected in favour of one uniform small face,
because the hierarchy was not worth its two costs.

The first was width. Measured by summing the font's glyph advances,
`KlimaControl` at 9 pt is **103 px**, which left the version only 79 px — 13
characters — so `v0.1.1-5-gc1c08f0` truncated to `v0.1.1-5-gc1.` on every
untagged developer build. In the small font the title is 72 px and the version
gets **110 px, i.e. 18 characters**: every version form in use fits whole, and
truncation reverts to being the guard it was meant to be.

The second was the **baseline trap**: the free fonts take `setCursor(x, y)` as
the glyph **baseline**, the built-in font as the glyph **top**, so a mixed row
needed two vertical constants that read like an off-by-seven. One font means one
constant. The trap still deserves a comment, because the rest of this file uses
free fonts and a reader may assume `HEADER_TOP_Y` is a baseline — it is not.

`getTextBounds()` reports correctly for both font families, so
`drawRightAligned()`, `textWidth()` and `fitToWidth()` work unchanged.

Vertical budget: ink runs `y 4..10`, clearing the window boundary at `y=30` by
20 px.

### D3. The version is truncated, not the brand mark

The footer's rule is "right column first, it is never truncated; the left column
gets what is left" (`EPaperDisplay.cpp:338`). The header inverts the priority
because the variable field is the one on the right: a tagged build is `v1.4.2`
(6 chars, 36 px) but the fallback is `v0.0.0-dev` (10 chars, 60 px) and a
`git describe` build is `v1.2.3-4-gabc1234` (17 chars, 102 px — see the comment
at `OTAUpdater.cpp:632`).

So: draw the title flush left at `FOOTER_MARGIN_X`, then right-align the version
into what remains, `fitToWidth()`-truncated with the existing trailing-`.`
marker. Tail truncation is the right direction for a version — it keeps the
release prefix and drops the build suffix. The brand mark is a fixed string and
must never be cut.

With both fields in the small font (D2) all three forms fit inside the 110 px
available, so this is a guard against a future version scheme rather than a path
the current builds take.

### D4. The version is read where it is drawn

`EPaperDisplay.cpp` includes `OTAConfig.h` and uses `FIRMWARE_VERSION` directly,
rather than adding a parameter to `render()` and `showSplash()` or a field to
`DisplayManager`. Threading a compile-time constant through two call layers and a
mutex-guarded tick buys nothing. `OTARoutes.cpp` and `StatusRoutes.cpp` already
consume the macro the same way, so this follows the established pattern.

### D5. The splash paints the band and loses its centred title

`showSplash()` uses `setFullWindow()` and `fillScreen(GxEPD_WHITE)`, so it would
blank the band; the band would then stay blank until the first value paint. Since
that first paint is always `Full` the gap is short, but it makes the boot screen
the only state where the header is missing — and the version is exactly what you
want to see if boot hangs.

Painting the band on the splash makes the splash's own centred `KlimaControl` at
`SPLASH_TITLE_Y=86` a duplicate of it. Rather than show the brand twice on one
200 px panel, the splash becomes: band, device name (12pt, centred), then
`starting...` (built-in font, centred below it). The boot screen then reads as a
state of the normal layout rather than a different screen.

The remaining pair is re-centred rather than left at the old baselines: with the
title line gone, keeping the name at `y=86` parks the block high on the panel, so
the name sits at `y=104` and the status line at `y=126`, centring the two of them
between the band and the bottom margin.

Alternative considered: keep the splash byte-identical and accept a blank band
for the first few seconds. Rejected as the version-during-boot case is precisely
the diagnostic one.

## Risks / Trade-offs

- **The band freezes if its content ever becomes dynamic.** Mitigated by making
  the static-content constraint an explicit spec requirement (D1), not a comment.
- **Ghosting in a never-partially-refreshed region.** The band is rewritten by
  every full refresh, i.e. at least every 12th partial
  (`FULL_REFRESH_EVERY_N_PARTIALS`), the same cadence that clears ghosting
  everywhere else. No new exposure.
- **`HEADER_TOP_Y` misread as a baseline.** Every other vertical constant in the
  file is one. Mitigated by the comment at the constant and by D2; the failure is
  visible immediately — text 7 px too low, or clipped off the top edge.
- **Two identities on one panel** — brand mark in the header, device name in the
  footer. Accepted deliberately: the brand orients a stranger, the name
  identifies the unit among several.

## Migration Plan

None. Display-only, and the panel is fully repainted on the first boot after the
update. No configuration, NVS or API surface changes.

## Open Questions

None blocking. Casing (`KlimaControl`) and the absence of a header rule were
decided before this design was written.
