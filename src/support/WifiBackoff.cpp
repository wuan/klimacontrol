#include "WifiBackoff.h"

namespace Support {

    uint32_t staFailureBackoffMs(uint8_t failures) {
        constexpr uint32_t BASE_MS = 2000;
        constexpr uint32_t CAP_MS = 300000; // 5 minutes
        if (failures == 0) failures = 1; // keep the shift well-defined
        // The cap is reached at failures=9 (2000 * 2^8 = 512000 > 300000).
        // Anything beyond is also the cap, and we must clamp the shift
        // amount to < 64 to avoid undefined behavior on the 64-bit shift.
        if (failures > 9) return CAP_MS;
        uint64_t delay = static_cast<uint64_t>(BASE_MS) << (failures - 1);
        if (delay > CAP_MS) delay = CAP_MS;
        return static_cast<uint32_t>(delay);
    }

} // namespace Support
