#include "network/InternetHealth.h"

#include "Log.h"

#ifdef ARDUINO
static constexpr auto TAG = "net";
#else
#define TAG "net"
#endif

namespace Net {

    void InternetHealth::reportFailure() {
        [[maybe_unused]] const uint32_t count = failureCount.fetch_add(1) + 1;
        ESP_LOGW(TAG, "Internet connectivity failure #%u", count);
    }

    void InternetHealth::reportSuccess() {
        failureCount.store(0);
        ESP_LOGD(TAG, "Internet connectivity success - reset failure counter");
    }

    void InternetHealth::reset(const uint32_t nowMs) {
        failureCount.store(0);
        lastActionMs = nowMs;
    }

    bool InternetHealth::shouldForceReconnect(const uint32_t nowMs) {
        if (failureCount.load() < FAILURE_THRESHOLD) return false;
        if (nowMs - lastActionMs < FAILURE_WINDOW_MS) return false;
        lastActionMs = nowMs;
        return true;
    }

} // namespace Net
