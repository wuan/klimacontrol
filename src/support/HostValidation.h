#ifndef KLIMACONTROL_HOST_VALIDATION_H
#define KLIMACONTROL_HOST_VALIDATION_H

#include <cstddef>

namespace Support {

    // True if `host` is an acceptable value for `DeviceConfig::actuator_host`.
    //
    // Empty is permitted and means "clear the assignment" — the existing
    // route contract for half-configured devices. A non-empty host must
    // match `^[A-Za-z0-9._-]{1,253}$` exactly. The character class is the
    // DNS-name alphabet with underscores allowed (mDNS service instances
    // use underscores; ordinary hostnames do not, and rejecting them
    // costs nothing). The length cap matches the DNS label limit.
    //
    // The validator rejects every byte that would let
    // `snprintf("http://%s%s", host, path)` in `HeatingActuator::httpGet()`
    // be redirected away from the configured manifold:
    //
    //   - URL metacharacters: `/`, `?`, `#`, `@`, `:` (the latter also
    //     rejects IPv6 literals, which the device does not support anyway)
    //   - Whitespace: space, tab, newline, carriage return
    //   - Any non-printable byte (< 0x20 or 0x7F)
    //
    // Address-range filtering (loopback, link-local, private range) is
    // intentionally out of scope: the Shelly is on the LAN and may
    // legitimately be in a private range, and silently refusing `10.0.0.1`
    // would be the wrong surprise. The SSRF concern is met by rejecting
    // URL metacharacters; address-range filtering is a separate, larger
    // policy decision.
    //
    // Pure C++, no Arduino-only headers. Safe to call from native tests.
    //
    // See `openspec/changes/2026-09-03-harden-config-ap-and-actuator-host/`
    // for the design rationale and the SSRF attack matrix.
    inline bool isValidActuatorHost(const char *host) {
        if (host == nullptr) {
            return false;
        }
        std::size_t length = 0;
        for (const char *p = host; *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            const bool inClass =
                (c >= '0' && c <= '9') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                c == '.' || c == '_' || c == '-';
            if (!inClass) {
                return false;
            }
            ++length;
            if (length > 253) {
                return false;
            }
        }
        // Empty is permitted.
        return true;
    }

} // namespace Support

#endif // KLIMACONTROL_HOST_VALIDATION_H
