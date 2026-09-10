#include "network/LowHeapGuard.h"

#include "Log.h"

#ifdef ARDUINO
static constexpr const char *const TAG = "net";
#else
#define TAG "net"
#endif

namespace Net {

    bool LowHeapGuard::sample(uint32_t freeInternalBytes) {
        if (freeInternalBytes >= MIN_FREE_INTERNAL_BYTES) {
            lowStreak = 0;
            return false;
        }
        lowStreak++;
        ESP_LOGW(TAG, "Low internal heap %u bytes (%u/%u consecutive)",
                 freeInternalBytes, lowStreak, RESTART_STREAK);
        if (lowStreak >= RESTART_STREAK) {
            ESP_LOGE(TAG, "CRITICAL: Low heap persisted - restarting...");
            return true;
        }
        return false;
    }

} // namespace Net
