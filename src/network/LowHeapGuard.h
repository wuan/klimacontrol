#ifndef KLIMACONTROL_NET_LOW_HEAP_GUARD_H
#define KLIMACONTROL_NET_LOW_HEAP_GUARD_H

#include <cstdint>

namespace Net {

    /**
     * Decides when to restart cleanly ahead of an out-of-memory crash. The
     * low condition has to persist across several consecutive ~1 s samples so
     * a transient dip (a burst of concurrent web requests each allocating a
     * JsonDocument) does not reboot a device that would otherwise recover.
     *
     * The caller samples *internal* SRAM only
     * (`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`): the allocations that
     * fail under pressure on this board — task stacks, lwIP/WiFi structures,
     * DMA buffers — are internal-only, while PSRAM stays healthy at ~2 MB.
     * Samples taken while an OTA download runs are ignored and reset the
     * streak, because the TLS session temporarily consumes most internal SRAM
     * by design.
     *
     * Note that `ESP.getFreeHeap()` is already internal-only (the Arduino core
     * implements it as `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`); naming
     * the capability at the call site is about explicitness. Verified on the
     * device: psram_size 2094735, ESP.getHeapSize() 166076 (internal total).
     */
    class LowHeapGuard {
    public:
        static constexpr uint32_t MIN_FREE_INTERNAL_BYTES = 16384; // 16 KB
        static constexpr uint8_t RESTART_STREAK = 5;

        /**
         * Record one sample. Returns true when the low condition has persisted
         * for RESTART_STREAK consecutive samples and the device should restart.
         */
        bool sample(uint32_t freeInternalBytes);

        /**
         * Forget the current low streak. Called when an OTA update ends: the
         * guard is not sampled while the download runs, so samples taken just
         * before it started must not carry over and trip a restart on the
         * first post-OTA tick.
         */
        void reset() { lowStreak = 0; }

        uint8_t streak() const { return lowStreak; }

    private:
        uint8_t lowStreak = 0;
    };

} // namespace Net

#endif // KLIMACONTROL_NET_LOW_HEAP_GUARD_H
