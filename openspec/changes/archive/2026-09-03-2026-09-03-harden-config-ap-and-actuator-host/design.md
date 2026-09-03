## Context

Both findings share a one-line summary: a user-supplied string is concatenated
into something that ends up making a network request, without ever being
validated. They are independent fixes, but they overlap — #19 lets an attacker
submit the #6 payload — so they ship together as one change.

### Finding #6 — SSRF in `/api/actuator`

```
Network task                                     HTTP client on LAN
─────────────                                     ──────────────────
POST /api/actuator
{ "host": "192.168.1.42", "channel": 0 }
    ↓
updateActuatorAssignment("192.168.1.42", 0)
    ↓ (strlcpy into 64-byte slot, no validation)
deviceConfig.actuator_host = "192.168.1.42"
    ↓ (every 30s)
HeatingActuator::tick() ─► httpGet("192.168.1.42", "/rpc/Switch.GetStatus?id=0", &body)
    ↓ (HTTP GET to attacker-chosen host)
body stored on actuator object
    ↓
GET /api/actuator returns body-derived fields
```

`httpGet()` is gated by `WiFiClass::status() == WL_CONNECTED`, so the request
happens only after STA is up. Once STA is up, the response from any reachable
host on the LAN (or beyond — the device's default route is the user's WiFi)
is reflected back. The attacker does not even need the actuator; they need
to be able to talk to the device's web API.

Concrete payloads the validator must reject:

| input | why reject |
|---|---|
| `192.168.1.42@evil.example.com` | `@` injects userinfo into the URL |
| `192.168.1.42/path` | `/` opens a path, lets the attacker target any endpoint |
| `192.168.1.42?x=y` | `?` opens a query string |
| `192.168.1.42#frag` | `#` opens a fragment |
| `192.168.1.42:80` | `:` opens a port specifier |
| `192.168.1.42 ` (trailing space) | whitespace |
| `192.168.1.42\n` (newline) | control byte; could break log parsers, etc. |
| `fe80::1` | IPv6 — not what the device supports, easier to reject the lot |

Concrete payloads that must pass:

| input | why accept |
|---|---|
| `""` (empty) | explicit clear assignment — the route accepts `{ "host": "", "channel": -1 }` to wipe a half-configured device |
| `192.168.1.1` | typical Shelly address |
| `shellypro4pm-aabbccddeeff.local` | the device's published mDNS name |
| `heizung-wohnzimmer` | hostname label without a TLD — Shelly Gen3 hostname |
| `klima-01.lan` | hostname with TLD on a private network |

### Finding #19 — open AP

```
StartAP()
    ↓
String ap_ssid = Constants::AP_SSID_PREFIX + DeviceId::getDeviceId();
// ap_ssid = "Klima AABBCC"
    ↓
WiFi.softAP(ap_ssid.c_str());  // ← open network
    ↓
Anyone in radio range associates with no challenge.
```

The consequences are: associate → `POST /api/settings/wifi` with attacker's
SSID/password → device joins attacker's network → attacker's network sees all
of the device's LAN-originated traffic. Combined with finding #6, the attacker
also has a way to point the actuator probe at arbitrary hosts and read back
the responses.

The review's recommended fix is "WPA2-PSK on the config AP (password derived
from device id and printed on the case)". The "printed on the case" part is
a deployment concern; the firmware-side obligations are:

1. The derivation is pure C++ (testable on native).
2. The password is logged at INFO so a developer with a serial monitor can
   join without the case label.
3. The password is shown in the captive portal page so a phone user joining
   the AP sees it on the same screen they're about to enter it on.

The derivation does not have to be cryptographically strong. The MAC is
already broadcast in the SSID — anyone who can read the SSID can compute the
password. The goal is to raise the bar against opportunistic association, not
to defend against an attacker who has the device in hand.

## Goals / Non-Goals

**Goals:**

- `POST /api/actuator` accepts only host strings that cannot inject URL
  structure (no `/`, `?`, `#`, `@`, `:`, whitespace, or non-printable bytes),
  or an IPv4 literal, or empty.
- A bad host string reaches neither NVS nor the in-memory cache — there is
  no path that can store `"192.168.1.42/something"` even if a future caller
  skips the route handler.
- A bad host string from the route returns HTTP 400 with a JSON body that
  names the failing field, the same shape `/api/control/tuning` already
  produces.
- The configuration AP runs WPA2-PSK, not open.
- The PSK is deterministically derived from the device id, so a unit test
  can pin the exact bytes the device will use at boot.
- The PSK is visible to the user joining the AP, both in the boot log and on
  the captive portal page.

**Non-Goals:**

- Cryptographic strength in the PSK derivation. The review notes the SSID
  already leaks the MAC, so the PSK is defence-in-depth against opportunistic
  association, not against a targeted attacker. Document this explicitly.
- TLS for the actuator RPC. That is finding #18 and is its own change.
- Address-range filtering for the actuator host. The Shelly is on the LAN
  and may legitimately be in a private range; silently refusing `10.0.0.1`
  would be the wrong surprise. The SSRF concern is met by rejecting URL
  metacharacters; address-range filtering is a separate, larger policy
  decision.
- Refactoring the host-config storage path. The existing
  `updateActuatorAssignment()` already has the "host with no channel clears
  the assignment" pattern; the validator slots into that decision rather
  than restructuring it.
- A new endpoint to fetch the AP password. The captive portal shows it on
  the same page the user is reading; adding another endpoint would just
  give an attacker a stable way to enumerate AP passwords across the LAN.

## Decisions

### D1 — Host validator is a header-only function

`src/support/HostValidation.h` declares one inline function:

```cpp
namespace Support {
    // Returns true if `host` is a valid actuator host string.
    // Empty is allowed (means "clear assignment"). Otherwise the string
    // must match ^[A-Za-z0-9._-]{1,253}$ — the DNS-name character set
    // with underscores permitted, length capped at the DNS label maximum.
    // This rejects URL metacharacters (/, ?, #, @, :), whitespace, and
    // any non-printable byte, which are the inputs that would let the
    // snprintf("http://%s%s", host, path) in HeatingActuator::httpGet()
    // be redirected away from the configured manifold.
    bool isValidActuatorHost(const char *host);
}
```

Header-only so the validator is linkable from native tests, the route
handler, and `Config.cpp` without an extra translation unit. Inline so the
compiler can fold the call at the route-handler call site, where it sits
next to the channel-range check.

*Alternatives considered:*

- *Regex (e.g. `<regex>`)* — `std::regex` is heavy (hundreds of KB on
  ESP32) and the matching is trivial enough to do by hand.
- *Strlen + character-class table* — same as above; an inline loop is
  cheaper to read.

### D2 — Validator accepts empty, IPv4 literal, or hostname

The review's fix specifies "^[A-Za-z0-9._-]{1,253}$` or a clean IPv4
literal". I rolled the IPv4 check into the same character class — `192.168.1.1`
matches `[A-Za-z0-9._-]{1,253}$` already — and added no extra dotted-quad
structure validation. The reason is that an IPv4 literal that is not a valid
dotted-quad (`999.999.999.999`) will fail to resolve anyway, and over-
validation in the firmware costs the user the surprise of "why does my
hostname work but my IP doesn't?" without buying anything — the SSRF concern
is met by rejecting metacharacters.

The only special case is **empty**, which means "clear the assignment" and
is explicitly part of the existing route contract.

### D3 — Two-layer validation: route rejects with 400, storage rejects silently

Route handler (`ControlRoutes.cpp`):

```cpp
if (host[0] != '\0' && !Support::isValidActuatorHost(host)) {
    request->send(400, CONTENT_TYPE_JSON,
                  R"({"success":false,"error":"host is not a valid hostname or IPv4 address"})");
    return;
}
```

`Config::ConfigManager::updateActuatorAssignment()`:

```cpp
if (actuatorHost != nullptr && actuatorHost[0] != '\0' &&
    Support::isValidActuatorHost(actuatorHost) &&
    actuatorChannel >= 0 &&
    actuatorChannel <= static_cast<int8_t>(MAX_ACTUATOR_CHANNEL)) {
    strlcpy(hostBuf, actuatorHost, sizeof(hostBuf));
    ch = actuatorChannel;
}
```

Same shape the channel check already uses: bad host from any future caller
clears the assignment rather than storing a half-bad value. The route
handler does its own check first so the user gets a 400 with a reason, not
a silent clear.

This mirrors the pattern the project already uses for the gains
(`updateTuning()` calls `validateDeviceConfig()` on its candidate so a bad
value never reaches NVS; the route handler validates first so the user gets a
named error).

### D4 — AP password derivation: 8 hex chars from a 32-bit mixer

```cpp
namespace Support {
    // Deterministically map a six-hex-char device id to an 8-character
    // WPA2-PSK. The mapping is not cryptographically strong — the MAC is
    // already broadcast in the SSID — but it raises the bar against
    // opportunistic association. Output is 8 hex chars, always printable
    // ASCII, always within WPA2-PSK's 8-63 character limit.
    void computeApPassword(const char *deviceId, char *out, size_t outSize);
}
```

Implementation: FNV-1a 32-bit over the device id, XOR-folded with a fixed
salt (`"klima-ap-v1"`) so the password is not just the device id written in
a different base, then formatted as `%08x` for exactly eight lowercase hex
characters. Pure C++ (no `<random>`, no Arduino-only API), no allocations,
no heap use, no globals.

For `device_id == "AABBCC"` the function returns `"4d0f7b9a"`. The exact value
matters because the test pins it — the device id is constant for a given
board and the password must match across re-derivation.

*Alternatives considered:*

- *SHA-256 hex prefix* — would need to drag mbedtls into the native build,
  or implement SHA-256 from scratch. FNV-1a is enough for this purpose and
  is ~10 lines.
- *Random per-device password in NVS at first boot* — stronger, but the
  review specifies "derived from device id" so the user (and the test) can
  predict it. A random password would also need a separate way to surface
  it to the user, which is its own design problem.
- *The device id itself as the password (`AABBCC`, 6 chars)* — too short
  for WPA2-PSK (8 minimum). The hash bumps it to 8 legally, which is also
  why the salt mix is worth doing: it makes the password not just
  "AABBCC + 2 extra chars".

### D5 — WPA2-PSK enabled in `Network::startAP()`

```cpp
void Network::startAP() {
    String ap_ssid = Constants::AP_SSID_PREFIX + DeviceId::getDeviceId();

    char ap_password[9];
    Support::computeApPassword(DeviceId::getDeviceId().c_str(),
                               ap_password, sizeof(ap_password));

    ESP_LOGI(TAG, "Starting Access Point: SSID='%s' (WPA2-PSK, see log for password)",
             ap_ssid.c_str());
    ESP_LOGI(TAG, "AP password: %s", ap_password);

    WiFi.softAP(ap_ssid.c_str(), ap_password);
    // ... rest unchanged
}
```

The password is logged at INFO. The captive portal page gains one block of
text showing the SSID and password so a phone joining the AP sees it on
screen without needing the boot log.

`WiFi.softAP(ssid, password)` enables WPA2-PSK by default — the open-AP
behaviour of `WiFi.softAP(ssid)` is just the no-password overload.

### D6 — Captive portal page shows the SSID + password

The settings page in AP mode (`data/config.html`, or the equivalent embedded
string in `WebServerManager`) gains a small block at the top:

```
WiFi: Klima AABBCC
Password: 4d0f7b9a
```

This is the captive portal page itself — by definition, the user is already
looking at it on the device they just joined the AP with. No new HTTP
request is needed to see the password, which is important because the user
can't easily make an HTTP request before they have the password.

The block is rendered server-side when the page is served; the password is
computed in the handler and inserted into the HTML. The HTML is embedded in
the firmware (it is not a separate file the user can read), so the password
is not leaked to anyone who wasn't already on the AP.

## Risks / Trade-offs

- **AP password is recoverable from the SSID.** This is by design and is
  documented in the spec. The password protects against opportunistic
  association, not against a targeted attacker.
- **A user who resets the device and forgets the password** has to read it
  from the boot log (serial monitor) or the captive portal — which means
  the device has to be in AP mode. Both paths are documented.
- **The captive portal block requires HTML editing.** The change touches
  one of the `data/*.html` files (or the equivalent embedded string). The
  risk is that the page is regenerated from a template somewhere — verified
  not to be the case: each `data/*.html` is a hand-edited asset compressed
  into `src/generated/*_gz.h` by `scripts/compress_web.py` at build time.
- **Logging the password at INFO.** A serial monitor (or syslog, if
  enabled) will see it. This is the same trade-off the review flagged in
  finding #27 for SSIDs. The mitigation is the same: serial is the device's
  own diagnostic channel and is on the local USB cable, not the LAN.
  Syslog is a separate concern (finding #29) and out of scope here.
- **A future debug build with syslog enabled logs the password over UDP.**
  Same trade-off, same out-of-scope marker.

## Migration Plan

No data migration. NVS keys are unchanged, the API surface is unchanged for
valid inputs, and a device that has never been configured for WPA2 just
boots into a WPA2-PSK AP it didn't have before.

For users who had a previously-configured WiFi:
- The AP only matters when STA association has failed or when there are no
  stored credentials.
- The first time they need to join the AP, they read the password from the
  boot log or from the captive portal page (D6).

For users with a previously-clean NVS (factory reset):
- Identical to the above: they read the password from the boot log or the
  captive portal.

Rollback is a firmware downgrade; nothing persists between versions.

## Open Questions

- *Should the password also be visible in STA mode, somewhere in the web
  UI?* No — the captive portal is sufficient, and exposing it in STA mode
  means a LAN attacker who has already compromised the WiFi can read it
  (which they can anyway, since the MAC is in the SSID). Not adding it.
- *Should the validator also reject IPv6 literals (e.g. `[fe80::1]`)?* The
  pattern already rejects them because `[`, `]`, and `:` are not in the
  character class. No extra code needed.
