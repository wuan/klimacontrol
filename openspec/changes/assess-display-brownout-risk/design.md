## Context

The e-paper panel draws a few milliamps idle and peaks around 25 mA while its
charge pump runs, for the ~2.6 s of a full refresh. On the QT Py ESP32-S2 that
load shares the 3.3 V regulator with the WiFi radio, whose TX bursts are the
suspected cause of the `ESP_RST_BROWNOUT` resets `src/main.cpp` already
instruments:

```cpp
// A ESP_RST_BROWNOUT here means the 3.3V rail sagged below the brownout
// threshold (typically the WiFi radio's TX inrush) and the chip reset
// instantly, which looks like a silent crash with no backtrace.
```

`add-eink-display` recommended a 100 µF bulk capacitor as a precaution and
documented it in the wiring guide, but the verification unit runs without one
and no brownout has been reported. So the guidance is currently unevidenced in
both directions.

Two things changed the exposure since that recommendation was written:

1. The clock trigger means a refresh roughly every minute rather than only on a
   significant value change — from a handful of refreshes an hour to ~60.
2. Every twelfth of those is a *full* refresh, the longest and heaviest
   transient, so ~5 an hour rather than a few a day.

## Goals / Non-Goals

**Goals:**

- Decide the capacitor question from observation rather than caution.
- Leave `docs/EINK_DISPLAY_WIRING.md` stating something definite.
- Keep brownouts diagnosable without a scope or a serial capture at the moment
  of failure.

**Non-Goals:**

- Instrumenting the supply rail. The boot-log reset reason is sufficient to
  answer "does this happen", which is the actual question.
- Tuning the brownout detector threshold. Lowering it to mask a real supply
  problem trades a clean reset for undefined behaviour.
- Battery or solar operation, where the rail behaves differently.

## Decisions

### D1. The boot log is the instrument

`resetReasonStr()` already prints `Reset reason: BROWNOUT (N)` on every boot, and
the core dump summary follows it. A brownout leaves no backtrace by definition —
the chip resets instantly — so the reset reason is the *only* signal, and it is
already in place. Nothing needs building to run this test.

Observation window: several hours with the display enabled, WiFi associated and
MQTT publishing, so refreshes and TX bursts are both occurring at their normal
rates. A device that survives that with no `BROWNOUT` line has demonstrated the
capacitor is not required for this build and this hardware.

### D2. Firmware mitigations are evaluated before hardware

If brownouts do appear, the ordering is deliberate — a firmware fix ships to
every existing unit over OTA, a capacitor does not:

1. **Widen the minimum refresh interval.** Already user-configurable. Fewer
   refreshes, fewer coincidences. Costs clock resolution.
2. **Suppress refreshes during WiFi TX.** Conceptually right, but the Arduino
   layer exposes no "radio is transmitting" signal, so this is speculative until
   investigated.
3. **Fit the capacitor.** Certain to work, but requires touching every unit.

Reaching for (3) first would be the easy call and the wrong one.

### D3. The documentation states a conclusion, not a hedge

§4 of the wiring guide currently says the capacitor is "cheap insurance", which
is what one writes before measuring. After this change it says either "required"
or "not required for a mains-powered unit — measured over N hours with no
brownout", with the evidence named. A reader deciding whether to solder should
not have to re-derive this.

## Risks / Trade-offs

- **Risk: the observation window is too short to catch a rare event.** A
  brownout needing an unlucky refresh/TX overlap might appear weekly, not
  hourly. → Accepted; the guide will state the window observed, so the claim is
  bounded by its evidence rather than overstated.
- **Risk: the result is specific to one unit's regulator, USB cable and supply.**
  → Also accepted and stated. A marginal cable is exactly the case where the
  capacitor earns its place, and the troubleshooting table already points at
  `Reset reason:` for anyone hitting it.
- **Trade-off: leaving the capacitor unfitted while investigating.** That is the
  point — fitting it first would destroy the measurement.

## Migration Plan

1. Confirm the display is enabled and the unit is running the current firmware.
2. Leave it running several hours under normal conditions.
3. Record every `Reset reason:` line; correlate any `BROWNOUT` with uptime and
   refresh cadence.
4. Decide, per D2.
5. Update `docs/EINK_DISPLAY_WIRING.md` §4 and the troubleshooting row.
6. Archive.

**Rollback.** Documentation only, unless a firmware mitigation is added.

## Open Questions

- How long is long enough? Starting at "several hours of normal operation" and
  extending if the result looks marginal.
- Does the Arduino ESP32 layer expose a usable "radio transmitting" signal for
  D2 option 2? Unknown, and only worth investigating if the observation finds a
  problem.
