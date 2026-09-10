#include "network/NtpSync.h"

#include "Log.h"
#include "network/InternetHealth.h"
#include "support/NetworkWatchdog.h"
#include "support/NtpEpoch.h"

static constexpr auto TAG = "net";

namespace Net {
    NtpSync::NtpSync()
#ifdef ARDUINO
        : client(udp)
#endif
    {
    }

    bool NtpSync::safeNtpUpdate() {
#ifdef ARDUINO
        return Support::guardedCall([this] { return client.forceUpdate(); });
#else
        return false;
#endif
    }

    uint32_t NtpSync::currentEpoch() const {
#ifdef ARDUINO
        if (!synced) return 0;
        return lastUpdateEpoch + (millis() - lastUpdateMs) / 1000U;
#else
        return 0;
#endif
    }

    void NtpSync::logTime(const char *what) const {
#ifdef ARDUINO
        ESP_LOGI(TAG, "NTP %s: %s", what, client.getFormattedTime().c_str());
#else
        (void) what;
#endif
    }

    NtpSync::Result NtpSync::attempt(const char *what, uint32_t &epoch) {
        if (!safeNtpUpdate()) return Result::Failed;
#ifdef ARDUINO
        epoch = client.getEpochTime();
#else
        epoch = 0;
#endif
        if (isNtpEpochPlausible(epoch)) return Result::Ok;
        bogusCount++;
        ESP_LOGE(TAG, "NTP %s returned implausible epoch: %u (expected between %u and %u)",
                 what, epoch, NtpEpoch::MIN_VALID, NtpEpoch::MAX_VALID);
        return Result::Implausible;
    }

    void NtpSync::begin() {
#ifdef ARDUINO
        // forceUpdate() gives a definite sync signal — update() short-circuits
        // and returns true if its internal interval hasn't elapsed, which would
        // not actually exchange any packets.
        ESP_LOGI(TAG, "Starting NTP...");
        client.begin();
#endif
        switch (uint32_t epoch = 0; attempt("initial sync", epoch)) {
            case Result::Ok:
                synced = true;
                lastUpdateEpoch = epoch;
                lastUpdateMs = millis();
                logTime("time");
                break;
            case Result::Implausible:
                // Stay unsynced; the retry in tick() will fire.
                break;
            case Result::Failed:
                ESP_LOGW(TAG, "NTP initial sync failed; will retry");
                break;
        }
    }

    void NtpSync::tick(const uint32_t nowMs, InternetHealth &health) {
        uint32_t epoch = 0;
        if (synced) {
            if (const uint32_t current = currentEpoch(); current - lastUpdateEpoch < UPDATE_INTERVAL_S) return;

            switch (attempt("update", epoch)) {
                case Result::Ok:
                    lastUpdateEpoch = epoch;
                    lastUpdateMs = millis();
                    logTime("update");
                    if (lastUpdateFailed) {
                        health.reportSuccess();
                        lastUpdateFailed = false;
                    }
                    break;
                case Result::Implausible:
                    // Keep the previous epoch and stay synced — do not flap into
                    // the unsynced state on a one-off bad refresh.
                    health.reportFailure();
                    break;
                case Result::Failed:
                    ESP_LOGW(TAG, "NTP update failed");
                    health.reportFailure();
                    lastUpdateFailed = true;
                    // Stay synced — the previous epoch is still usable, just stale.
                    break;
            }
            return;
        }

        if (nowMs - lastRetryMs < UNSYNCED_RETRY_MS) return;
        lastRetryMs = nowMs;

        switch (attempt("retry", epoch)) {
            case Result::Ok:
                synced = true;
                lastUpdateEpoch = epoch;
                lastUpdateMs = millis();
                logTime("initial sync");
                if (lastUpdateFailed) {
                    health.reportSuccess();
                    lastUpdateFailed = false;
                }
                break;
            case Result::Implausible:
                // Stay unsynced; the retry timer will fire again.
                break;
            case Result::Failed:
                health.reportFailure();
                lastUpdateFailed = true;
                break;
        }
    }
} // namespace Net
