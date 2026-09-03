#ifndef KLIMACONTROL_DISPLAY_EPAPERDISPLAY_H
#define KLIMACONTROL_DISPLAY_EPAPERDISPLAY_H

#ifdef ARDUINO

#include <cstdint>

#include "display/RefreshPolicy.h"

namespace Display {

    /**
     * Thin wrapper over the GxEPD2 driver for the Waveshare 1.54" V2 panel.
     *
     * Owns the paged draw loop, the watchdog feeds around it, and the BUSY
     * fault guard. Knows nothing about when to refresh — that is RefreshPolicy.
     */
    class EPaperDisplay {
    public:
        // A refresh taking longer than this is counted as a timeout. A normal
        // full refresh is ~2.6 s (GxEPD2_154_D67::full_refresh_time), so this
        // is generous, while staying inside the 30 s task watchdog.
        static constexpr uint32_t REFRESH_TIMEOUT_MS = 12000;

        // Consecutive timeouts before the display gives up until reboot.
        static constexpr uint8_t MAX_CONSECUTIVE_TIMEOUTS = 3;

        /**
         * Bring up the SPI bus and the panel.
         *
         * @param rotation 0..3, passed to GxEPD2's setRotation()
         * @return false if the panel faulted during initialisation
         */
        bool begin(uint8_t rotation);

        /**
         * True once `begin()` has successfully initialised the panel and
         * the BUSY line has settled. Distinct from `hasFaulted()` — a
         * panel can be initialised but later faulted, and a probe can
         * observe the panel without ever calling begin().
         */
        bool isInitialised() const { return initialised && !faulted; }

        /**
         * Detect whether an e-paper panel is physically present on the
         * connector. The probe drives a manual reset pulse on RST and
         * watches BUSY for the LOW-then-HIGH transition that a healthy
         * panel produces while it processes and then releases the
         * reset. A missing or damaged panel does not transition and
         * the probe returns false.
         *
         * The probe does NOT touch the SPI bus, does NOT call GxEPD2's
         * `display.init()`, and does NOT modify `initialised` or
         * `faulted`. It is purely a presence check, suitable for
         * deciding whether to bring the panel up at boot or at
         * AP-mode entry.
         *
         * Conservative: it returns true only when both BUSY
         * transitions (LOW then HIGH) are observed within
         * `timeoutMs`. A missing panel (BUSY never goes LOW), a
         * stuck-LOW BUSY (damaged panel), and a stuck-HIGH BUSY
         * (interference / damaged line) all read as "no panel" —
         * which is the safer failure mode (a false positive would
         * lock the user out of the configuration AP). See change
         * `fix-display-probe-busy-transitions` for the rationale.
         *
         * `#ifdef ARDUINO` — the native test build provides a stub that
         * returns false. Display detection is hardware-only.
         *
         * @param timeoutMs Total budget for the probe, including both
         *                  transitions and the reset pulse. 250 ms is
         *                  generous for a healthy panel.
         * @return true if the BUSY line shows the panel responding.
         */
        static bool probe(uint32_t timeoutMs);

        /**
         * Full-refresh a boot splash showing the device name.
         */
        void showSplash(const char *deviceName);

        /**
         * Full-refresh a dedicated AP-info screen. Assumes `begin()`
         * has already been called. No-op if the panel is not
         * initialised or has faulted.
         *
         * Layout: brand mark in the header band (same as the normal
         * display), then three labelled lines — "SSID: ...", "Password:
         * ...", "IP: ...". The watchdog is fed before and after the
         * paged draw loop, matching the blocking-call-safety rule in
         * `system-architecture`. See change
         * 2026-09-04-ap-password-via-display.
         *
         * @param ssid     AP SSID, e.g. "Klima AABBCC"
         * @param password AP password, e.g. "abcdef01"
         * @param ip       AP IP address, typically "192.168.4.1"
         */
        void showApInfo(const char *ssid, const char *password, const char *ip);

        /**
         * Paint the current values.
         *
         * The footer is two lines in two columns: the device name over the
         * date/time on the left, the setpoint over the control symbol on the
         * right. Left-column fields are truncated to whatever the right column
         * leaves free, so a long device name cannot overrun the setpoint.
         *
         * @param tempStr     Preformatted temperature (no unit suffix)
         * @param humStr      Preformatted humidity (no unit suffix)
         * @param footerName  Footer line 1, left column (device name)
         * @param footerDateTime Footer line 2, left column (date and time);
         *                    may be empty while NTP is unsynced
         * @param controlState Control state, drawn as footer line 2, right column
         * @param setpointStr Preformatted setpoint, footer line 1, right column
         * @param demandSegments Filled segments of the demand bar, 0..DEMAND_BUCKETS.
         *                    Drawn on footer line 2 between the date/time and
         *                    the control symbol, and only while controlState is
         *                    not INACTIVE. Pre-bucketed by the caller so the
         *                    panel repaints on a visible change, not every tick.
         * @param kind        Partial repaints the value+footer window; Full also
         *                    clears ghosting
         */
        void render(const char *tempStr, const char *humStr,
                    const char *footerName, const char *footerDateTime,
                    ControlState controlState, const char *setpointStr,
                    uint8_t demandSegments, RefreshKind kind);

        /**
         * Blank the panel to white. Used on the disable path — e-paper retains
         * its image unpowered, so a disabled display must be actively cleared.
         */
        void clear();

        /**
         * Put the panel into deep sleep. The image is retained.
         */
        void hibernate();

        /**
         * True once MAX_CONSECUTIVE_TIMEOUTS refreshes in a row have exceeded
         * REFRESH_TIMEOUT_MS. Further refresh attempts are skipped until the
         * device restarts. Deliberately in-memory only: a hardware fault must
         * not rewrite the user's persisted configuration.
         */
        bool hasFaulted() const { return faulted; }

    private:
        bool initialised = false;
        bool faulted = false;
        uint8_t consecutiveTimeouts = 0;

        // Runs the paged draw loop for the current window. The footer is drawn
        // unconditionally: it carries a live clock and lies entirely inside the
        // partial-refresh window, so it stays in step with the values on every
        // refresh.
        void drawDemandBar(int16_t leftX, uint8_t filledSegments);

        // Paints the header band: the brand mark flush left, the firmware
        // version flush right, both in the built-in 5x7 font. Called from
        // inside the paged draw loop, where a partial refresh clips it away —
        // the band lies above the partial window and so carries only
        // compile-time-constant content. Leaves the built-in font selected, so
        // callers must set the font they need next.
        void drawHeader();

        void drawMeasurements(const char *tempStr, const char *humStr);

        void drafFooter(const char *footerName, const char *footerDateTime, Display::ControlState controlState,
                        const char *setpointStr, uint8_t demandSegments);

        void runPagedDraw(const char *tempStr, const char *humStr,
                          const char *footerName, const char *footerDateTime,
                          ControlState controlState, const char *setpointStr,
                          uint8_t demandSegments);

        // Records the duration of a completed panel operation and trips the
        // fault guard when the timeout streak is reached.
        void noteDuration(uint32_t elapsedMs, const char *what);
    };

} // namespace Display

#else // !ARDUINO

// Native stub: the probe is hardware-only and cannot run on the host. The
// native test build gets a deterministic "no panel" answer so any code path
// that branches on the probe result can be tested in isolation. See change
// 2026-09-04-ap-password-via-display for the rationale.
namespace Display {
    class EPaperDisplay {
    public:
        static bool probe(uint32_t /*timeoutMs*/) { return false; }
    };
} // namespace Display

#endif // ARDUINO

#endif // KLIMACONTROL_DISPLAY_EPAPERDISPLAY_H
