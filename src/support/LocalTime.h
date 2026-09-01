#ifndef KLIMACONTROL_SUPPORT_LOCALTIME_H
#define KLIMACONTROL_SUPPORT_LOCALTIME_H

#include <cstddef>
#include <cstdint>

// Local time handling, built on the POSIX timezone facilities newlib already
// provides. Deliberately free of Arduino and FreeRTOS dependencies so the whole
// thing builds and is tested in the `native` environment — and because the host
// libc implements the same POSIX TZ grammar, those tests exercise the real
// conversion rather than a mock.
//
// The timezone is a POSIX TZ string carrying BOTH the offset and the
// daylight-saving transition rules:
//
//   CET-1CEST,M3.5.0,M10.5.0/3
//   └┬┘└┬┘└─┬┘ └──┬──┘└───┬───┘
//    │  │   │     │       └── DST ends:   last Sunday of October, 03:00
//    │  │   │     └────────── DST starts: last Sunday of March, 02:00 (default)
//    │  │   └──────────────── DST abbreviation
//    │  └──────────────────── offset west of UTC, i.e. UTC+1
//    └─────────────────────── standard abbreviation
//
// Storing the rules rather than a bare offset is what makes the clock correct
// year-round without the firmware owning a table of per-region transition dates
// that changes by legislation.
namespace Support {

    // Applied when nothing is configured, or when the stored value is unusable.
    // Reproduces the firmware's pre-timezone behaviour of showing UTC.
    constexpr const char *DEFAULT_TIMEZONE = "UTC0";

    // Longest POSIX string accepted, excluding the terminator. Real-world rule
    // strings run to ~35 characters.
    constexpr size_t MAX_TIMEZONE_LEN = 47;

    /**
     * Set the process timezone. Subsequent local-time conversions resolve in
     * this zone, daylight saving included.
     *
     * A null, empty or implausible argument falls back to DEFAULT_TIMEZONE, so
     * a corrupted NVS read degrades to UTC rather than to undefined tzset()
     * behaviour.
     */
    void applyTimezone(const char *tz);

    /**
     * Shape check for a POSIX TZ string.
     *
     * Deliberately permissive: the POSIX grammar admits quoted designations
     * (`<+04>-4`) and fractional offsets (`IST-5:30`), and a strict parser
     * would reject valid input for no benefit. Anything that slips through and
     * is malformed is interpreted by tzset() as UTC, which is the same outcome
     * as no configuration at all.
     */
    bool isPlausibleTimezone(const char *tz);

    /**
     * Format `epoch` as "HH:MM" in the configured zone.
     *
     * @return characters written, or 0
     *
     * Writes an empty string and returns 0 when `epoch` is 0 — the established
     * "NTP not yet synced" sentinel. Rendering the Unix epoch's local
     * representation (01:00 in CET) would be actively misleading.
     */
    size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch);

    /**
     * Format `epoch` as "YYYY-MM-DD" in the configured zone. Same sentinel and
     * null-safety behaviour as formatLocalHhMm().
     */
    size_t formatLocalDate(char *out, size_t n, uint32_t epoch);

    /**
     * Format `epoch` as "YY-MM-DD HH:MM" in the configured zone — a two-digit
     * year, for places where the full date and time have to share a line with
     * other content. Same sentinel and null-safety behaviour as
     * formatLocalHhMm().
     *
     * Needs a 15-byte buffer. A shorter one truncates rather than overflowing,
     * as with the other formatters here.
     */
    size_t formatLocalDateHhMm(char *out, size_t n, uint32_t epoch);

} // namespace Support

#endif // KLIMACONTROL_SUPPORT_LOCALTIME_H
