#ifndef KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H
#define KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H

#ifdef ARDUINO

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Config.h"
#include "display/EPaperDisplay.h"
#include "display/RefreshPolicy.h"

class SensorController;
class Network;

namespace Display {

    /**
     * Owns the e-paper panel and its refresh scheduling.
     *
     * Long-lived singleton constructed at file scope in main.cpp, alongside
     * StatusLed. update() is driven from the Network task's one-second loop;
     * it is a no-op unless the refresh policy calls for a repaint, so the
     * common path never touches the SPI bus.
     */
    class DisplayManager {
    public:
        explicit DisplayManager(SensorController &controller);

        DisplayManager(const DisplayManager &) = delete;
        DisplayManager &operator=(const DisplayManager &) = delete;

        /**
         * Initialise the panel and paint the boot splash. Only called when the
         * display is enabled in configuration.
         *
         * @param config    Validated display configuration
         * @param deviceName Name shown on the splash and in the footer
         * @return false if the panel faulted during initialisation
         */
        bool begin(const Config::DisplayConfig &config, const char *deviceName);

        /**
         * Stop refreshing, blank the panel, and put it to sleep.
         *
         * Called from the web request handler when the display is switched off:
         * e-paper retains its image with no power, so a display that has been
         * disabled has to be actively cleared or it keeps showing a stale
         * reading forever.
         *
         * Blocks for the duration of a full refresh (~2.6 s), plus up to
         * another full refresh if the Network task happens to be mid-repaint
         * when this is called.
         */
        void disableAndClear();

        /**
         * Blank the panel without changing the `enabled` flag. Used by
         * the Network task when the user submits new WiFi credentials
         * from AP mode and the device is about to restart into STA
         * mode: clearing the panel wipes the AP info (SSID + password
         * + IP) so it does not persist through the restart, while
         * leaving `enabled` alone means the user's DisplayConfig
         * preference is preserved for the next boot.
         *
         * No-op if the panel is not initialised. Blocks for up to one
         * full refresh (~2.6 s).
         */
        void clear();

        /**
         * Bring the panel up on demand for AP info rendering. Returns
         * `true` when the e-paper panel is available, `false` otherwise.
         *
         * If the manager is already in normal operation (`enabled ==
         * true`), the function returns `true` without touching anything
         * — the panel is obviously present and usable.
         *
         * Otherwise the function calls `panel.probe(timeoutMs)` first
         * (the BUSY-transition check described under
         * `display` → *E-paper panel can be probed by BUSY pin
         * transition*) and returns `false` immediately on a failed
         * probe — the probe catches the no-panel case that
         * `panel.begin()` alone misses, because
         * `GxEPD2::display.init()` silently succeeds when no panel
         * is connected. Only on a successful probe does the
         * function call `panel.begin(config.rotation)`, which runs
         * `GxEPD2::display.init()` (the proven init path) and
         * returns its result; `panel.begin()` returning false after
         * a successful probe is treated as "no display" and the
         * function returns false. See change
         * `fix-display-probe-busy-transitions` for the rationale.
         *
         * No-op on the panel state when the manager was already enabled
         * — the panel's `initialised` flag is left as-is.
         */
        bool tryBeginForApInfo(const Config::DisplayConfig &config);

        /**
         * Paint the AP info screen and set `apModeActive` so the normal
         * `update()` tick does not paint temperature on top of the
         * password. No-op if the panel is not initialised.
         */
        void showApInfo(const char *ssid, const char *password, const char *ip);

        /**
         * Clear `apModeActive`. A panel in normal operation is left
         * alone; the STA-mode `update()` tick takes over after the user
         * submits WiFi credentials and the device restarts.
         */
        void endApInfo();

        /**
         * True while the AP info screen is showing. `update()` checks
         * this and skips painting on top of the password.
         */
        bool isApModeActive() const { return apModeActive; }

        /**
         * Evaluate the current measurements and repaint if the policy says so.
         * Safe to call every second; returns immediately in the common case.
         */
        void update();

        /**
         * Supplies the footer clock. Set by main.cpp once Network exists;
         * nullptr simply means no clock is drawn.
         */
        void setNetwork(Network *net) { network = net; }

        bool isEnabled() const { return enabled; }
        bool hasFaulted() const { return panel.hasFaulted(); }

    private:
        SensorController &controller;
        Network *network = nullptr;

        EPaperDisplay panel;
        RefreshPolicy policy;

        // Currently displayed demand bar segments. Held here rather than in
        // RefreshPolicy because nextDemandBucket() needs the previous value to
        // apply its hysteresis, and the policy should only ever be asked
        // "did this change?".
        uint8_t demandBucket = 0;

        bool enabled = false;
        bool apModeActive = false;
        char deviceName[32] = "";

        // Serialises panel access between the Network task (periodic update())
        // and the AsyncTCP task (disableAndClear() from the web handler).
        // Without it the two can drive GxEPD2 and the SPI bus concurrently,
        // corrupting the transfer or leaving a BUSY wait hanging.
        SemaphoreHandle_t panelMutex = nullptr;

        // Renders the current local date and time as "YY-MM-DD HH:MM" into
        // `out`, or an empty string when NTP has not synced (or no Network is
        // wired in) — the footer then stays blank rather than claiming a time.
        void formatDateTime(char *out, size_t n) const;
    };

} // namespace Display

#endif // ARDUINO

#endif // KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H
