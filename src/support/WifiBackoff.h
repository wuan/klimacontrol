#ifndef KLIMACONTROL_WIFI_BACKOFF_H
#define KLIMACONTROL_WIFI_BACKOFF_H

#include <cstdint>

namespace Support {

    /**
     * Compute the delay (in milliseconds) to wait before restarting after
     * a failed boot-time STA association. The schedule is a doubling curve
     * starting at 2000 ms and capped at 300000 ms (5 minutes), so a
     * transient AP outage has time to recover before the device tears down
     * its association state again. The curve is:
     *
     *   failures=1   ->  2000 ms
     *   failures=2   ->  4000 ms
     *   failures=3   ->  8000 ms
     *   failures=7   -> 120000 ms
     *   failures=8+  -> 300000 ms (saturates)
     *
     * The `failures` argument is clamped to 1 so the bit shift stays
     * well-defined if a future caller passes 0. See the spec
     * `network-wifi-resilience` → "Exponential backoff on boot-time STA
     * failure" for the contract.
     */
    uint32_t staFailureBackoffMs(uint8_t failures);

} // namespace Support

#endif // KLIMACONTROL_WIFI_BACKOFF_H
