#ifndef KLIMACONTROL_SUPPORT_NTP_EPOCH_H
#define KLIMACONTROL_SUPPORT_NTP_EPOCH_H

#include <cstdint>

// Plausibility bounds for an NTP epoch. Any sync whose resulting epoch falls
// outside this range is treated as a failed sync. 2020-01-01T00:00:00Z gives
// a 6+ year floor margin from 2026; 2100-01-01 gives a 75-year ceiling
// margin. The check catches 0 (the documented "not yet synced" sentinel in
// the mqtt-integration spec), near-zero corruption, and obviously bogus
// values. See the "NTP time synchronization" requirement in
// openspec/specs/networking/spec.md.
//
// Defined at namespace scope (rather than inside Network) so unit tests can
// include this header without pulling in the full Network class hierarchy
// (which transitively requires FreeRTOS).
namespace NtpEpoch {
    constexpr uint32_t MIN_VALID = 1577836800; // 2020-01-01T00:00:00Z
    constexpr uint32_t MAX_VALID = 4102444800; // 2100-01-01T00:00:00Z
}

inline bool isNtpEpochPlausible(uint32_t epoch) {
    return epoch >= NtpEpoch::MIN_VALID && epoch <= NtpEpoch::MAX_VALID;
}

#endif // KLIMACONTROL_SUPPORT_NTP_EPOCH_H
