#ifndef KLIMACONTROL_DISPLAY_REFRESHPOLICY_H
#define KLIMACONTROL_DISPLAY_REFRESHPOLICY_H

#include <cstddef>
#include <cstdint>

// Refresh scheduling for the e-paper display.
//
// Deliberately free of Arduino and FreeRTOS dependencies so the whole decision
// state machine builds and runs in the `native` PlatformIO environment. The
// caller supplies the clock (`nowMs`) rather than this code calling millis(),
// which is what lets the unit tests drive time directly — including across the
// ~49.7 day rollover.
namespace Display {

    /**
     * What the display should do with a new set of values.
     */
    enum class RefreshKind {
        None,    // Nothing to do; do not touch the SPI bus
        Partial, // Rewrite the value window only (~0.3 s, no flash)
        Full     // Rewrite the whole panel (~2 s, black/white flash, clears ghosting)
    };

    // A reading must move by at least this much before a refresh is considered.
    // Sensor noise dithering the last displayed digit would otherwise refresh
    // the panel forever with no visible difference.
    constexpr float TEMP_HYSTERESIS_C = 0.1f;
    constexpr float HUMIDITY_HYSTERESIS_PCT = 1.0f;

    // Partial refreshes accumulate ghosting; every Nth one is promoted to a
    // full refresh to clear it.
    constexpr uint8_t FULL_REFRESH_EVERY_N_PARTIALS = 12;

    /**
     * Decides whether — and how — to repaint the panel.
     *
     * The three brakes are independent because each catches a different
     * failure: hysteresis suppresses sensor noise, the minimum interval bounds
     * the worst case when a value is genuinely sweeping, and the partial
     * counter bounds ghosting.
     */
    class RefreshPolicy {
    public:
        explicit RefreshPolicy(uint16_t minIntervalSec);

        /**
         * Evaluate a new set of values.
         *
         * @param temperature Current temperature; NAN is treated as unavailable
         * @param humidity    Current relative humidity; NAN is treated as unavailable
         * @param valid       Whether the sensor snapshot itself is valid
         * @param nowMs       Monotonic millisecond clock (millis() on the firmware)
         * @param clockMinute Displayed wall-clock minute, i.e. epoch / 60; pass
         *                    0 when the clock is unknown (NTP unsynced). A
         *                    change here is treated as a change worth showing,
         *                    so the footer timestamp advances on its own.
         *                    Defaults to 0 so callers with no clock — and the
         *                    value-only unit tests — are unaffected.
         * @return What kind of refresh to perform, if any
         */
        RefreshKind evaluate(float temperature, float humidity, bool valid, uint32_t nowMs,
                             uint32_t clockMinute = 0);

        /**
         * Forget all history, as if the device had just booted. The next
         * evaluate() returns Full.
         */
        void reset();

        /**
         * Minimum seconds between refreshes.
         */
        uint16_t getMinIntervalSec() const { return minIntervalSec; }

        /**
         * Partial refreshes performed since the last full refresh. Exposed for
         * tests and diagnostics.
         */
        uint8_t getPartialsSinceFull() const { return partialsSinceFull; }

    private:
        uint16_t minIntervalSec;

        bool everPainted = false;    // false until the first refresh is returned
        bool lastValid = false;      // validity of the last *rendered* values
        float lastTemperature = 0.0f;
        float lastHumidity = 0.0f;
        uint32_t lastRefreshMs = 0;
        uint32_t lastClockMinute = 0; // wall-clock minute at the last refresh
        uint8_t partialsSinceFull = 0;

        // Records the values a refresh is about to render and returns `kind`.
        // Only called when a refresh actually happens, so a change suppressed
        // by the interval floor stays outstanding and fires on a later tick.
        RefreshKind commit(RefreshKind kind, float temperature, float humidity,
                           bool valid, uint32_t nowMs, uint32_t clockMinute);
    };

    /**
     * Format a temperature for the panel: one decimal place, or "--.-" when the
     * value is unavailable. Returns the number of characters written.
     */
    size_t formatTemperature(char *out, size_t n, float value, bool valid);

    /**
     * Format a relative humidity for the panel: whole number, or "--" when the
     * value is unavailable. Returns the number of characters written.
     */
    size_t formatHumidity(char *out, size_t n, float value, bool valid);

} // namespace Display

#endif // KLIMACONTROL_DISPLAY_REFRESHPOLICY_H
