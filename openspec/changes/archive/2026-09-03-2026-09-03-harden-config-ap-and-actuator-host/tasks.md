# Tasks: harden config AP (WPA2-PSK) and `/api/actuator` host validation

Ordered so each step is independently testable. Sections 1-4 cover #6 (host
validation); sections 5-8 cover #19 (WPA2-PSK on the config AP).

## 1. Spec scaffolding

- [x] 1.1 `openspec validate 2026-09-03-harden-config-ap-and-actuator-host --strict`
      passes against the artifacts in `openspec/changes/2026-09-03-harden-config-ap-and-actuator-host/`

## 2. Host validator

- [x] 2.1 Create `src/support/HostValidation.h` with a single inline function
      `bool Support::isValidActuatorHost(const char *host)`. Empty string
      returns true (means "clear assignment"); otherwise the entire string
      must match `^[A-Za-z0-9._-]{1,253}$`. Reject any input containing a
      null byte before the terminator, any non-printable byte, or any byte
      outside the character class
- [x] 2.2 Header must be self-contained (no Arduino-only includes) so it
      links from the native test build without a `#ifdef ARDUINO` guard

## 3. Route handler rejects invalid host

- [x] 3.1 In `src/routes/ControlRoutes.cpp`, in the `POST /api/actuator`
      handler, add a check on `host` between the existing JSON parse and the
      existing channel-range check. On rejection, respond HTTP 400 with
      `{"success":false,"error":"host must be empty, an IPv4 address, or a hostname (letters, digits, '.', '_', '-')"}`
- [x] 3.2 The empty host case stays valid (the existing route accepts it to
      clear a half-configured assignment) — only validate when `host[0] != '\0'`

## 4. Storage boundary validates defensively

- [x] 4.1 In `src/Config.cpp::updateActuatorAssignment()`, AND the existing
      host/channel check with `Support::isValidActuatorHost(actuatorHost)`.
      Same shape: bad input clears the assignment, never stores a bad value
- [x] 4.2 Add an include for `support/HostValidation.h` at the top of
      `src/Config.cpp` next to the existing `WifiBackoff` / `MqttClient`
      includes

## 5. Host validation tests

- [x] 5.1 Create `test/test_actuator_host_validation/` with a single
      `test_actuator_host_validation.cpp`. Add to the `native` env
      `build_src_filter` in `platformio.ini` (the directory gets discovered
      automatically, but the host validator is header-only so this is only
      about being explicit about the new test directory; if 8's finding
      still holds, just add the test directory and rely on auto-discovery
      — verify by `pio test -e native --list-tests`)
- [x] 5.2 Test: empty string is valid
- [x] 5.3 Test: typical IPv4 literals (`192.168.1.1`, `10.0.0.1`,
      `127.0.0.1`, `0.0.0.0`) are valid
- [x] 5.4 Test: typical hostnames (`shelly.local`, `klima-01.lan`,
      `shellypro4pm-aabbccddeeff.local`, `Heizung-1`, `a`) are valid
- [x] 5.5 Test: 253-character host is valid; 254-character host is rejected
- [x] 5.6 Test: each URL metacharacter in turn is rejected (`/`, `?`, `#`,
      `@`, `:`, ` `, `\t`, `\n`, `\r`, `\\`)
- [x] 5.7 Test: IPv6 literals (`fe80::1`, `::1`, `[fe80::1]`) are rejected
- [x] 5.8 Test: each printable byte outside the character class is rejected
      (e.g. `!`, `$`, `*`, `(`, `)`, `,`, `;`, `'`, `"`, `<`, `>`)
- [x] 5.9 Test: null input is rejected (defensive; caller should pass `""`)
- [x] 5.10 Test: the SSRF attack matrix from the design — `192.168.1.42@evil`,
      `192.168.1.42/path`, `192.168.1.42?x=y`, `192.168.1.42:80`,
      `192.168.1.42 ` (trailing space), `192.168.1.42\n` (newline) — all
      rejected

## 6. AP password derivation

- [x] 6.1 Create `src/support/ApPassword.h` with one inline function
      `void Support::computeApPassword(const char *deviceId, char *out, size_t outSize)`.
      Output is 8 lowercase hex chars plus a NUL terminator (so `outSize` must
      be ≥ 9; the function returns without writing if it is not). The
      implementation is FNV-1a 32-bit over `deviceId`, XOR-folded with a
      fixed salt `"klima-ap-v1"`, formatted as `%08x`
- [x] 6.2 Header must be self-contained (no Arduino-only includes) and
      produce the same output on native and ESP32

## 7. WPA2-PSK on the config AP

- [x] 7.1 In `src/Network.cpp::startAP()`, after computing `ap_ssid`, also
      compute `ap_password` via `Support::computeApPassword(deviceId, ...)`
      and log both SSID and password at INFO. Add `#include "support/ApPassword.h"`
      next to the existing `WifiBackoff.h` include
- [x] 7.2 Replace `WiFi.softAP(ap_ssid.c_str())` with
      `WiFi.softAP(ap_ssid.c_str(), ap_password)` to enable WPA2-PSK
- [x] 7.3 The captive portal page (settings page in AP mode) gains a one-
      line block showing SSID and password. The password is computed in the
      handler rather than passed via a query parameter so the URL doesn't
      carry it. Verify the HTML is not a generated file (no template that
      regenerates it on every build) and is the embedded form already used
      by `WebServerManager.cpp` in AP mode

## 8. AP password tests

- [x] 8.1 Create `test/test_ap_password/test_ap_password.cpp` with the
      Unity harness
- [x] 8.2 Test: a 6-hex device id produces exactly 8 lowercase hex chars
      (`[0-9a-f]{8}`)
- [x] 8.3 Test: the same device id always produces the same password
      (idempotence — a future re-derivation must produce the same bytes)
- [x] 8.4 Test: distinct device ids produce distinct passwords (no
      accidental collisions in the 8-hex output space for a small sample)
- [x] 8.5 Test: a specific pin value — `deviceId = "000000"` produces the
      exact password the implementation is expected to, recorded in the
      test comment. This is the regression-catching test: if the FNV salt
      or mixing ever changes, this fails
- [x] 8.6 Test: output buffer of size 8 (one too small) is rejected — the
      function leaves the buffer unmodified and does not write a partial
      password
- [x] 8.7 Test: null input produces 8 `0` chars and does not crash

## 9. Verify and close out

- [x] 9.1 `pio test -e native` — all green, including the two new suites
- [x] 9.2 `pio run -e adafruit_qtpy_esp32s2` — builds and reports the same
      flash / RAM headroom as the baseline (the new headers are inline
      functions; no allocations on the hot path)
- [x] 9.3 Verify the captive portal page renders correctly in AP mode by
      reading the modified HTML and confirming the password block is in
      the right place (manual test on hardware is out of scope for this
      change)
- [x] 9.4 Re-read `routes/ControlRoutes.cpp` and `Config.cpp` end-to-end and
      confirm every code path that ends up calling
      `httpGet("http://%s%s", host, path)` can only be reached with a host
      that has passed `isValidActuatorHost()`
- [x] 9.5 Update `docs/CODE_REVIEW.md` findings #6 and #19 to mark them
      resolved, in the shape of the existing entries (point at the change
      name and the files touched, link to the change under
      `openspec/changes/archive/` after archive)
- [x] 9.6 `/opsx:archive`
