## Why

The previous change (`2026-09-03-harden-config-ap-and-actuator-host`) ships
WPA2-PSK with a password deterministically derived from the device id. The
discovery story has a gap:

| Discovery path | Requires |
|---|---|
| Serial log | USB cable + serial monitor |
| `/api/ap-info` rendered on the captive portal | already on the AP (chicken-and-egg) |
| Case label | printed by the production line |
| Compute from device id | the algorithm documented in the spec |

Without a serial console and without a case label, the user has no way to
discover the password. The captive-portal block I added in the previous
change gives the *illusion* of discoverability — the password is on screen,
but only for someone who already has it.

The e-paper display is a natural fit:

- It is already wired into the case, visible at the moment the user needs
  the password (they are standing in front of the device).
- E-paper retains its image unpowered, so the password stays on screen
  across reboots until the panel is actively cleared.
- A user who has no serial cable and no case label can still read the
  password directly off the device.

A device with the display has a working discovery path; a device without
it does not. The cleanest way to capture this is to make the AP security
mode depend on display presence:

- **Display detected** → WPA2-PSK with the derived password, rendered on
  the display during AP mode. Anyone in radio range still cannot join
  without the password.
- **No display** → fall back to open AP. The user with no other channel
  (no serial, no case label) gets a usable setup path. The trade-off is
  that anyone in radio range can also associate, but a device with no
  display and no other way to discover the password would otherwise be
  unconfigurable.

The probe uses the BUSY pin on the e-paper connector. After a manual reset
pulse, a connected panel pulls BUSY HIGH then LOW within ~100 ms; a missing
panel does not transition, and the probe times out. The probe is
**conservative**: it returns "display detected" only when it has observed a
real BUSY transition. A false positive (probe says "display present" when
none is) locks the user out, which is the worse failure; a false negative
(probe says "no display" when one is) just falls back to open AP, which
is no worse than the pre-fix behaviour.

## What Changes

- `EPaperDisplay::probe(uint32_t timeoutMs)` — static, hardware-only BUSY
  pin probe. Returns true only if the BUSY line goes HIGH then LOW after a
  reset pulse within the timeout. Does not modify any panel state.
- `EPaperDisplay::showApInfo(ssid, password, ip)` — paints a dedicated
  AP-info screen. Assumes `begin()` has been called.
- `DisplayManager::tryBeginForApInfo(config)` — probes the panel and, if
  alive, brings it up for one-shot AP info rendering. Independent of
  `DisplayConfig.enabled`: a device with the display disabled in config but
  physically present still shows the AP info in AP mode.
- `DisplayManager::showApInfo(ssid, password, ip)` and
  `DisplayManager::endApInfo()` — wrappers around the panel methods.
  `endApInfo()` hibernates the panel only when the manager inited it
  itself (i.e. `DisplayConfig.enabled == false`); a panel in normal
  operation is left alone.
- `Network::setApPassword(const char *)` — stores the password before the
  network task starts. Empty string means "open AP". main.cpp decides
  what to set based on the probe result.
- `Network::startAP()` — uses the configured password for `WiFi.softAP`,
  or `WiFi.softAP(ssid)` alone if no password was configured. Renders
  the AP info on the display when one is available.
- `main.cpp` setup() — probes the display, computes the password (or
  leaves it empty), configures Network accordingly. Boot-time cost: a
  single ~250 ms probe, only on the path that needs AP info.
- Spec requirements added in `networking` ("AP security depends on
  display presence", "AP password is shown on the e-paper display when
  present") and `display` ("E-paper panel can be probed by BUSY pin
  transition", "AP info screen renders SSID, password, IP").

### Non-goals

- A `DisplayConfig` flag to force WPA2-PSK without a display. If a user
  wants WPA2-PSK without a display, they can connect a display
  temporarily, configure WiFi, then disconnect. Adding a config flag
  invites the false-positive lock-out risk we are explicitly avoiding.
- TLS for the captive portal. Separate change.
- Per-user pairing / a button-press to bring up the AP. Separate change.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `networking`: add requirements that the AP security mode depends on
  whether a display was detected at boot, and that the password is
  rendered on the display when one is present.
- `display`: add requirements for the BUSY-pin probe and the AP-info
  screen layout.

## Impact

- **Source**: `src/display/EPaperDisplay.{h,cpp}` (probe, showApInfo),
  `src/display/DisplayManager.{h,cpp}` (tryBeginForApInfo, showApInfo,
  endApInfo), `src/Network.{h,cpp}` (setApPassword, modified startAP),
  `src/main.cpp` (probe, configure Network with the decision).
- **Tests**: native tests for the `DisplayManager::tryBeginForApInfo` /
  `showApInfo` paths where possible (pure-C++ smoke tests of the
  decision logic without the hardware probe).
- **Spec**: delta in `openspec/changes/<this>/specs/{networking,display}/spec.md`.
- **No NVS schema change**, no firmware version bump.
- **Behaviour change for existing devices**: a device with a previously
  configured WiFi still operates identically — it never enters AP mode.
  The new probe only runs at boot before the network task starts.
- **Behaviour change for unconfigured devices** (the AP-mode case): the
  previous change always enabled WPA2-PSK. This change enables WPA2-PSK
  when a display is detected, and falls back to open AP otherwise. The
  fallback is intentional and documented in the spec.
