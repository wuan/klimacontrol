#ifndef KLIMACONTROL_AP_PASSWORD_H
#define KLIMACONTROL_AP_PASSWORD_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace Support {

    // Deterministically map a six-hex-char device id to an 8-character
    // WPA2-PSK passphrase for the configuration AP.
    //
    // `out` receives a NUL-terminated 8-character lowercase hex string
    // plus its terminator, so `outSize` must be at least 9. If it is
    // smaller, the function leaves `out` unmodified and does not write a
    // partial password. `deviceId` is treated as a NUL-terminated string;
    // passing `nullptr` is treated as if an empty string had been passed.
    //
    // The mapping is FNV-1a 32-bit over `deviceId`, XOR-folded with a
    // fixed salt so the password is not just the device id in a different
    // base, formatted as `%08x`. Pure C++, no Arduino-only headers, no
    // allocations, no globals — safe to call from native tests and from
    // Network::startAP() in the Network task.
    //
    // **Not cryptographically strong.** The MAC is already broadcast in
    // the AP SSID (`Klima <device-id>`), so anyone who can read the SSID
    // can compute the password. The purpose is to raise the bar against
    // opportunistic association by a passerby who has not seen the SSID,
    // not to defend against a targeted attacker. See change
    // 2026-09-03-harden-config-ap-and-actuator-host for the design.
    inline void computeApPassword(const char *deviceId, char *out, std::size_t outSize) {
        if (out == nullptr || outSize < 9) {
            return;
        }
        // FNV-1a 32-bit. The mix starts from the offset basis and folds
        // each input byte in, then folds a fixed salt on top so the
        // password is not a trivial permutation of the device id.
        uint32_t h = 0x811c9dc5u;
        if (deviceId != nullptr) {
            for (const char *p = deviceId; *p != '\0'; ++p) {
                h ^= static_cast<uint8_t>(*p);
                h *= 0x01000193u;
            }
        }
        static const char salt[] = "klima-ap-v1";
        for (const char *p = salt; *p != '\0'; ++p) {
            h ^= static_cast<uint8_t>(*p);
            h *= 0x01000193u;
        }
        std::snprintf(out, outSize, "%08x", static_cast<unsigned>(h));
    }

} // namespace Support

#endif // KLIMACONTROL_AP_PASSWORD_H
