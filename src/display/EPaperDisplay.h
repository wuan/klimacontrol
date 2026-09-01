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
         * @param rotation 0..3, passed to GxEPD2's setRotation()
         * @return false if the panel faulted during initialisation
         */
        bool begin(uint8_t rotation);

        /**
         * Full-refresh a boot splash showing the device name.
         */
        void showSplash(const char *deviceName);

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
         * @param kind        Partial repaints the value+footer window; Full also
         *                    clears ghosting
         */
        void render(const char *tempStr, const char *humStr,
                    const char *footerName, const char *footerDateTime,
                    ControlState controlState, const char *setpointStr,
                    RefreshKind kind);

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
        void runPagedDraw(const char *tempStr, const char *humStr,
                          const char *footerName, const char *footerDateTime,
                          ControlState controlState, const char *setpointStr);

        // Records the duration of a completed panel operation and trips the
        // fault guard when the timeout streak is reached.
        void noteDuration(uint32_t elapsedMs, const char *what);
    };

} // namespace Display

#endif // ARDUINO

#endif // KLIMACONTROL_DISPLAY_EPAPERDISPLAY_H
