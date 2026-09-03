## Context

The previous change shipped WPA2-PSK with a password deterministically
derived from the device id, on the assumption that the user could read it
from the boot log or from a case label. The follow-up question — "how do I
get the password without a serial console?" — exposed the gap: there is no
self-contained discovery path for a device with no serial cable and no case
label. The captive-portal block on the previous change (`/api/ap-info`)
gives the illusion of one (the password is on the page), but the page is
only reachable after joining the AP, which requires the password.

The e-paper display is the one place the user can see the password
*before* joining the AP:

1. It is mounted to the case, visible at the moment the user needs the
   password.
2. It is reachable without any external connection (no serial, no WiFi,
   no browser).
3. It retains its image unpowered, so a re-rendered AP info screen stays
   on display across reboots.

The catch: a device without the display is back to "no discovery path",
because the password cannot be communicated. The previous change left such
a device unable to join its own AP, which is worse than the pre-fix
open-AP behaviour.

The cleanest resolution is to make the AP security mode depend on
display presence:

- Display detected at boot → WPA2-PSK, password rendered on display.
- No display → open AP, no password needed.

## Goals / Non-Goals

**Goals:**

- A device with the e-paper display connected can be configured without a
  serial cable and without a case label: the password is on the panel
  during AP mode.
- A device without the e-paper display can still be configured: the AP is
  open, and the user joins it without typing anything.
- The detection is conservative: it returns "display detected" only when
  there is real evidence of a panel responding. A missing or damaged
  panel reads as "no display", which falls back to open AP (no worse than
  pre-fix).
- The decision is made once at boot and held for the lifetime of that
  boot. There is no mid-boot flip in either direction.

**Non-Goals:**

- A configuration flag to force WPA2-PSK without a display. A user who
  needs that behaviour can connect a display temporarily, configure WiFi,
  then disconnect; the cost of a flag is a future lock-out bug.
- A button-press alternative for AP mode. Separate change.
- Detecting the display *type* (this is only the Waveshare 1.54" V2;
  other panels would need a different probe).
- Probe on every AP-mode entry. The probe runs once at boot.

## Decisions

### D1 — Probe is a static method on `EPaperDisplay`

```cpp
// src/display/EPaperDisplay.h
class EPaperDisplay {
public:
    // True if a panel responds to a reset pulse within `timeoutMs` by
    // pulling BUSY HIGH then LOW. Pure hardware probe — does not modify
    // any panel state, does not claim the SPI bus.
    static bool probe(uint32_t timeoutMs);
};
```

The probe temporarily configures CS/DC/RST/BUSY pins and runs the panel
reset sequence (`RST` high → low → high), then watches the BUSY line. A
healthy panel goes BUSY=HIGH (busy with reset) then BUSY=LOW (idle);
a missing panel does neither.

The probe is **static** so it can be called without a `DisplayManager`
instance. The function does not need the static GxEPD2 `display`
object — it works directly with the GPIO pins and the BUSY read, leaving
the GxEPD2 state untouched.

### D2 — Probe budget is 250 ms total

The e-paper panel reset completes in well under 100 ms on a healthy
device. The probe budget is set to 250 ms:

- BUSY=HIGH within 100 ms (panel reset acknowledged)
- BUSY=LOW within 100 ms after that (reset complete)
- 50 ms slack for SPI bus contention / panel warm-up

The budget is a single `timeoutMs` parameter; the function waits for
BUSY=HIGH first, then BUSY=LOW, with the total time capped at
`timeoutMs`. The caller can choose to pass a smaller value if a tighter
budget is needed (none do today).

### D3 — Decision lives in main.cpp, consumed by the Network task

```cpp
// src/main.cpp setup()
const bool displayDetected = Display::EPaperDisplay::probe(250);
char apPassword[9] = "";
if (displayDetected) {
    const String deviceId = DeviceId::getDeviceId();
    Support::computeApPassword(deviceId.c_str(), apPassword, sizeof(apPassword));
}
network.setApPassword(displayDetected ? apPassword : "");
```

`Network::setApPassword` stores the password (or "" for open AP). The
Network task reads it back in `startAP()`:

```cpp
void Network::startAP() {
    const String deviceId = DeviceId::getDeviceId();
    String ap_ssid = Constants::AP_SSID_PREFIX + deviceId;

    char password[9];
    const bool useWpa2 = getApPassword(password, sizeof(password));

    if (useWpa2) {
        WiFi.softAP(ap_ssid.c_str(), password);
    } else {
        WiFi.softAP(ap_ssid.c_str());
    }
}
```

The decision is held in Network for the boot lifetime. There is no mid-boot
flip: if the probe said "no display" at boot, the AP is open throughout;
if it said "display present", the AP is WPA2-PSK throughout.

*Alternatives considered:*

- *Probe in `Network::startAP()`* — would couple display detection to AP
  mode entry. The probe is cheap (~250 ms), and AP mode is rare, but the
  one-shot-at-boot approach makes the decision predictable and easier to
  reason about.
- *Probe in `DisplayManager`* — DisplayManager is constructed at file
  scope; it could probe in its constructor. That would couple the
  decision to a constructor argument order, and DisplayManager is the
  wrong place to make a network security decision. Keeping the probe
  static and the decision in main.cpp respects the layering.

### D4 — `DisplayManager::tryBeginForApInfo` is independent of `DisplayConfig.enabled`

```cpp
class DisplayManager {
public:
    // Bring up the panel for one-shot AP info rendering.
    //
    // If the manager is already in normal operation (DisplayConfig.enabled
    // was true at boot), nothing to do — return true.
    //
    // If the manager is dormant (no normal operation), probe the BUSY pin.
    // If the probe succeeds, init the panel; if it fails, leave the
    // manager dormant and return false. The caller treats a false return
    // as "no display detected" and falls back to open AP.
    bool tryBeginForApInfo(const Config::DisplayConfig &config);
};
```

`DisplayConfig.enabled` covers the normal status display (temperature,
humidity, setpoint). The AP-info screen is a different use of the same
panel and is enabled by hardware presence, not by user preference. A user
who has disabled the normal display but left the panel connected can
still see the AP info — which is exactly the discovery path we are
providing.

### D5 — `showApInfo` is one-shot, not periodic

The Network task does not repaint the AP info every tick. `showApInfo` is
called once when the AP comes up, then the panel is either:

- Left in normal operation (DisplayConfig.enabled was true). The next
  `update()` tick will paint temperature/humidity on top of the AP info,
  unless we suppress it.
- Hibernate'd (DisplayConfig.enabled was false). The image stays on the
  panel until the user clears it or the panel is next init'd.

The first case is annoying: the user wants to read the AP password but
the panel is showing temperature instead. To handle this, `update()`
checks an `apModeActive` flag and bails out while it is set:

```cpp
void DisplayManager::update() {
    if (!enabled || panel.hasFaulted() || apModeActive) {
        return;
    }
    ...
}
```

`enterApMode(ssid, password, ip)` sets the flag and calls
`panel.showApInfo`. `exitApMode()` clears it. The Network task calls
`enterApMode` when it brings up the AP and `exitApMode` when the AP
ends (after the user submits credentials and the device restarts into
STA mode).

For the second case (display not in normal operation), we don't need an
`apModeActive` flag — `update()` is already a no-op because `enabled`
is false. We just call `panel.showApInfo` once and hibernate.

### D6 — Open AP is the only fallback

The previous change shipped WPA2-PSK unconditionally and accepted the
risk of lock-out. This change accepts the risk of open AP when no display
is detected. The reasoning:

- A device with no display, no serial cable, and no case label cannot
  communicate the password to its user. WPA2-PSK would leave the device
  unconfigurable; open AP is the only path that works.
- A device that has a display or a serial cable gets WPA2-PSK and the
  password is reachable.
- A device that has a case label gets WPA2-PSK, and the user reads the
  password off the case.

The fallback is documented in the spec ("Configuration AP runs WPA2-PSK
when a display is detected, otherwise open").

## Risks / Trade-offs

- **False-positive probe.** A broken BUSY pin that happens to read HIGH
  after reset would say "display detected" when there is no panel. The
  user would be locked out. The probe is conservative — it requires the
  full HIGH-then-LOW transition — so this should only happen on a panel
  with a damaged BUSY line (rare) or with external interference (very
  rare). The user can recover via serial or reflash.
- **Display-init fails after probe succeeds.** The probe is a pure pin
  dance; the init goes through GxEPD2. If GxEPD2's `display.init()`
  fails (panel faulted during init), `tryBeginForApInfo` returns false
  and we fall back to open AP. This treats the panel as if it weren't
  there, which is the safe choice.
- **Probe cost.** ~250 ms added to boot, on the path that may need AP
  info. Acceptable.
- **Display pins claimed at boot.** Even if `DisplayConfig.enabled` is
  false, the probe configures CS/DC/RST/BUSY as outputs/inputs. If the
  panel is not present, the CS pin is left HIGH (deselected) and the
  RST pin is HIGH (idle). The BUSY pin is left as an input. None of
  these interfere with the WiFi or sensor buses; the static asserts in
  `DisplayPins.h` already guard against collisions.
- **User changes DisplayConfig while device is in AP mode.** If the user
  disables the display via web UI while the device is up in AP mode, the
  panel is cleared by `disableAndClear()` and the password is no longer
  visible. This is the user explicitly opting out; they have access to
  serial as a fallback.
- **Behaviour change for users with no display on an unconfigured
  device.** Previously: WPA2-PSK with the derived password, no way to
  read it. Now: open AP. The user has to decide whether the new
  behaviour is acceptable. The change is documented in the spec and the
  CODE_REVIEW resolution.

## Migration Plan

No data migration. NVS keys are unchanged. The API surface is unchanged
(valid inputs only — the validator from the previous change still applies).
A device with a previously configured WiFi operates identically.

For a user with an unconfigured device and no display:

- Before: WPA2-PSK with a password they cannot read.
- After: open AP. They connect to `Klima AABBCC`, get the captive
  portal, configure WiFi.

For a user with an unconfigured device and a display:

- Before: WPA2-PSK, password on the captive portal (chicken-and-egg).
- After: WPA2-PSK, password on the display and the captive portal.

Rollback is a firmware downgrade; nothing persists between versions.

## Open Questions

- *Should the probe also detect a panel that has been intentionally
  disconnected mid-run?* No. The decision is made at boot and held.
  Mid-run disconnect is a hardware fault, not a configuration state.
- *Should the user be able to opt out of the display-based decision and
  force open AP?* Already possible: connect without a display, and the
  AP is open.
- *Should the user be able to force WPA2-PSK without a display?* Not
  in this change. The flag is easy to add later if needed; the risk of
  lock-out is real, and adding a flag invites the bug where the flag
  is set wrong.
