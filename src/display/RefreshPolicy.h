#ifndef KLIMACONTROL_DISPLAY_REFRESHPOLICY_H
#define KLIMACONTROL_DISPLAY_REFRESHPOLICY_H

#include <cmath>
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

    /**
     * What the footer's control symbol shows. Lives here rather than in
     * EPaperDisplay.h because the refresh decision depends on it and this header
     * is the Arduino-free half of the display code.
     */
    enum class ControlState {
        INACTIVE,   // control switched off
        ACTIVE_OFF, // enabled, valve confirmed closed
        ACTIVE_ON,  // enabled, heating confirmed
        // Enabled and a channel is assigned, but the actuator has not been
        // observed recently, or the relay and the actuator disagree. Shown
        // distinctly because the alternative is a confident symbol the device
        // cannot vouch for — and the panel is the thing people actually look at.
        UNCERTAIN
    };

    // A reading must move by at least this much before a refresh is considered.
    // Sensor noise dithering the last displayed digit would otherwise refresh
    // the panel forever with no visible difference.
    constexpr float TEMP_HYSTERESIS_C = 0.1f;
    constexpr float HUMIDITY_HYSTERESIS_PCT = 1.0f;

    // Partial refreshes accumulate ghosting; every Nth one is promoted to a
    // full refresh to clear it.
    constexpr uint8_t FULL_REFRESH_EVERY_N_PARTIALS = 12;

    // Controller demand is shown as a coarse bar rather than a number. The
    // panel repaints whenever a displayed value changes, so a live percentage
    // would pin refreshes at the minimum-interval floor forever — roughly six
    // times the idle rate — for a value that means little at single-percent
    // resolution on a plant with hours-long dynamics. Five buckets carry the
    // useful information (off / low / half / high / full) and cross a boundary
    // rarely.
    constexpr uint8_t DEMAND_BUCKETS = 5;

    // Quantising alone is not enough: a demand hovering on a boundary would
    // flip buckets every tick and repaint just as often. The same reasoning as
    // TEMP_HYSTERESIS_C above, applied to the bucket edges.
    constexpr float DEMAND_BUCKET_HYSTERESIS = 0.03f;

    /**
     * Bucket for a demand fraction in [0, 1], given the bucket currently shown.
     *
     * Returns 0..DEMAND_BUCKETS, i.e. the number of filled segments. Moving to
     * a neighbouring bucket requires clearing the boundary by
     * DEMAND_BUCKET_HYSTERESIS, so a value sitting on an edge stays put.
     *
     * @param fraction Demand as a fraction of the output range; NAN yields 0.
     * @param previous The bucket currently displayed.
     */
    uint8_t nextDemandBucket(float fraction, uint8_t previous);

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
         * @param setpoint     Target temperature shown in the footer; NAN when
         *                     unknown. A change is a change worth showing, so
         *                     retargeting from the web UI repaints on the next
         *                     tick instead of waiting for the reading to move.
         * @param controlState Control symbol shown in the footer; same reasoning.
         * @param demandBucket Filled segments of the demand bar, 0..DEMAND_BUCKETS.
         *                     Already hysteretic — see nextDemandBucket() — so
         *                     this is a plain comparison, not another threshold.
         * @return What kind of refresh to perform, if any
         */
        RefreshKind evaluate(float temperature, float humidity, bool valid, uint32_t nowMs,
                             uint32_t clockMinute = 0, float setpoint = NAN,
                             ControlState controlState = ControlState::INACTIVE,
                             uint8_t demandBucket = 0);

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
        float lastSetpoint = NAN;     // setpoint at the last refresh
        ControlState lastControlState = ControlState::INACTIVE;
        uint8_t lastDemandBucket = 0;
        uint8_t partialsSinceFull = 0;

        // Records the values a refresh is about to render and returns `kind`.
        // Only called when a refresh actually happens, so a change suppressed
        // by the interval floor stays outstanding and fires on a later tick.
        RefreshKind commit(RefreshKind kind, float temperature, float humidity,
                           bool valid, uint32_t nowMs, uint32_t clockMinute,
                           float setpoint, ControlState controlState, uint8_t demandBucket);
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
