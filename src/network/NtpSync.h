#ifndef KLIMACONTROL_NET_NTP_SYNC_H
#define KLIMACONTROL_NET_NTP_SYNC_H

#include <cstdint>

#ifdef ARDUINO
#include <NTPClient.h>
#include <WiFiUdp.h>
#endif

namespace Net {

    class InternetHealth;

    /**
     * NTP time source for the Network task.
     *
     * NTPClient::getEpochTime() returns elapsed-since-boot before any
     * successful sync (its _currentEpoc is 0 and millis-since-_lastUpdate
     * accumulates), so "epoch > 0" is not a usable synced indicator; the
     * synced state is tracked explicitly here, and every sync result has to
     * pass the plausibility window in `support/NtpEpoch.h`.
     *
     * Schedule: one attempt right after WiFi comes up (`initialSync`), then
     * from `tick()` a retry every UNSYNCED_RETRY_MS while unsynced and a
     * refresh every UPDATE_INTERVAL_S once synced. A failed refresh keeps the
     * previous (stale but usable) epoch rather than flapping to unsynced.
     */
    class NtpSync {
    public:
        static constexpr uint32_t UPDATE_INTERVAL_S = 3600;
        static constexpr uint32_t UNSYNCED_RETRY_MS = 60000;

        NtpSync();

        NtpSync(const NtpSync &) = delete;
        NtpSync &operator=(const NtpSync &) = delete;

        /** Start the UDP client and try one sync. Call once WiFi is connected. */
        void begin();

        /**
         * Periodic work; call once per second. Refreshes and retries report to
         * `health` so a dead internet link behind a live WiFi association is
         * noticed. The caller must not call this while an OTA update is in
         * progress: a stalled UDP exchange would block the task for the NTP
         * timeout and its failure would be counted as an outage even though
         * the link is merely saturated by the download. The clock tolerates
         * the delay; the interval check re-fires as soon as OTA is done.
         */
        void tick(uint32_t nowMs, InternetHealth &health);

        /** Current epoch seconds, or 0 before the first successful sync. */
        uint32_t currentEpoch() const;

        bool isSynced() const { return synced; }
        uint32_t bogusSyncCount() const { return bogusCount; }

        /**
         * Run ntpClient.forceUpdate() with the task watchdog fed immediately
         * before and after. forceUpdate() is bounded by NTPClient's 1 s
         * timeout, but the underlying WiFiUDP::parsePacket() can stall for
         * tens of seconds on a degraded link (lwIP retransmits, ARP retries)
         * before that timeout gets a chance to fire; unguarded, one hung call
         * could exceed the 30 s TWDT and panic-reboot the device. See
         * `support/NetworkWatchdog.h` and the spec `networking` → "Network
         * task blocking-call safety".
         */
        bool safeNtpUpdate();

    private:
        enum class Result : uint8_t { Ok, Implausible, Failed };

        /**
         * One guarded exchange plus plausibility check. On Ok, `epoch` holds
         * the new time; an implausible result is counted and logged with
         * `what` naming the call site.
         */
        Result attempt(const char *what, uint32_t &epoch);
        void logTime(const char *what) const;

#ifdef ARDUINO
        WiFiUDP udp;
        NTPClient client;
#endif
        bool synced = false;
        uint32_t lastUpdateEpoch = 0; // epoch seconds at last successful sync
        uint32_t lastUpdateMs = 0;    // millis() captured with lastUpdateEpoch
        uint32_t bogusCount = 0;      // syncs that passed the boolean check but failed the epoch sanity check
        uint32_t lastRetryMs = 0;     // millis() of last retry while unsynced
        bool lastUpdateFailed = false;
    };

} // namespace Net

#endif // KLIMACONTROL_NET_NTP_SYNC_H
