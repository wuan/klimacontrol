#ifndef KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H
#define KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H

#ifdef ARDUINO

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
         * Blank the panel and put it to sleep. Used on the disable path, where
         * the panel would otherwise keep showing a stale reading forever.
         */
        void clearAndPark(const Config::DisplayConfig &config);

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

        bool enabled = false;
        char deviceName[32] = "";

        // Renders the current time as HH:MM into `out`, or an empty string when
        // NTP has not synced (or no Network is wired in).
        void formatClock(char *out, size_t n) const;
    };

} // namespace Display

#endif // ARDUINO

#endif // KLIMACONTROL_DISPLAY_DISPLAYMANAGER_H
