## 1. Observe

- [ ] 1.1 Confirm the unit is running the current firmware with the display enabled, WiFi associated and MQTT publishing, so refreshes and TX bursts both occur at their normal rates
- [ ] 1.2 Confirm the 100 µF bulk capacitor is **not** fitted — fitting it before the observation would destroy the measurement
- [ ] 1.3 Record the refresh cadence in effect (the configured minimum interval, and therefore roughly how many full refreshes per hour) so the observation window can be expressed in refreshes, not just hours
- [ ] 1.4 Leave the unit running for several hours under normal conditions
- [ ] 1.5 Capture every `Reset reason:` line seen during the window. A clean run means no `BROWNOUT` at all; note the total uptime and estimated refresh count

## 2. Decide

- [ ] 2.1 If no brownout occurred: record the window (hours, approximate refresh count) as the evidence that bulk decoupling is not required for a mains-powered unit on this hardware
- [ ] 2.2 If brownouts occurred: correlate each against uptime and refresh cadence to confirm the display is implicated rather than WiFi alone
- [ ] 2.3 If implicated, evaluate the firmware mitigations in the order set out in design D2 — widen the minimum refresh interval first, then investigate whether the Arduino layer exposes a usable "radio transmitting" signal — and record the outcome of each before mandating hardware
- [ ] 2.4 Only if firmware mitigation is insufficient, fit the 100 µF capacitor and re-run section 1 to confirm it resolves the issue

## 3. Document

- [ ] 3.1 Rewrite `docs/EINK_DISPLAY_WIRING.md` §4 to state the conclusion definitively, replacing the current "cheap insurance" wording, and name the observation it rests on
- [ ] 3.2 State the conditions the measurement was taken under (mains/USB powered, this regulator, this refresh cadence) so the claim is bounded by its evidence
- [ ] 3.3 Update the "Device reboots during a refresh" row of the troubleshooting table to match the conclusion
- [ ] 3.4 If a firmware mitigation was adopted, document it in the README's E-Paper Display section alongside the other refresh-policy behaviour

## 4. Close out

- [ ] 4.1 Run `pio test -e native` and `pio run -e adafruit_qtpy_esp32s2` if any source changed; skip if the change was documentation only
- [ ] 4.2 Archive the change with `/opsx:archive`
