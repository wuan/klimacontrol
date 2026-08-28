#include "support/LocalTime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace Support {

    namespace {
        // Shared by both formatters: resolve an epoch to broken-down local time.
        // Returns false for the unsynced sentinel or an unconvertible value.
        bool localBrokenDown(uint32_t epoch, struct tm &out) {
            if (epoch == 0) {
                return false; // NTP not yet synced
            }
            const time_t t = static_cast<time_t>(epoch);
            return localtime_r(&t, &out) != nullptr;
        }

        // Common guard for the formatters. Returns false when there is nowhere
        // to write; otherwise leaves `out` as an empty string.
        bool prepareBuffer(char *out, size_t n) {
            if (out == nullptr || n == 0) {
                return false;
            }
            out[0] = '\0';
            return true;
        }
    } // namespace

    bool isPlausibleTimezone(const char *tz) {
        if (tz == nullptr) {
            return false;
        }
        const size_t len = strnlen(tz, MAX_TIMEZONE_LEN + 1);
        if (len == 0 || len > MAX_TIMEZONE_LEN) {
            return false;
        }
        for (size_t i = 0; i < len; i++) {
            const unsigned char c = static_cast<unsigned char>(tz[i]);
            if (c < 0x20 || c > 0x7E) {
                return false; // non-printable or non-ASCII
            }
        }
        return true;
    }

    void applyTimezone(const char *tz) {
        const char *value = isPlausibleTimezone(tz) ? tz : DEFAULT_TIMEZONE;
        setenv("TZ", value, 1);
        tzset();
    }

    size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch) {
        if (!prepareBuffer(out, n)) {
            return 0;
        }
        struct tm lt;
        if (!localBrokenDown(epoch, lt)) {
            return 0;
        }
        const int written = snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
        return written < 0 ? 0 : static_cast<size_t>(written);
    }

    size_t formatLocalDate(char *out, size_t n, uint32_t epoch) {
        if (!prepareBuffer(out, n)) {
            return 0;
        }
        struct tm lt;
        if (!localBrokenDown(epoch, lt)) {
            return 0;
        }
        const int written = snprintf(out, n, "%04d-%02d-%02d",
                                     lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
        return written < 0 ? 0 : static_cast<size_t>(written);
    }

} // namespace Support
