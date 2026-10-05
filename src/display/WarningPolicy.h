#ifndef KLIMACONTROL_DISPLAY_WARNINGPOLICY_H
#define KLIMACONTROL_DISPLAY_WARNINGPOLICY_H

#include <cmath>
#include <cstdint>

// Warning-icon decision logic for the e-paper display.
//
// Deliberately free of Arduino and FreeRTOS dependencies so the whole decision
// state machine builds and runs in the `native` PlatformIO environment — the
// same discipline as RefreshPolicy. The caller supplies the clock (`nowMs`)
// rather than this code calling millis(), which is what lets the unit tests
// drive time directly, including across the millis() rollover.
namespace Display {

    /**
     * Which warning the panel's icon slot shows. Declaration order is
     * priority order: when several conditions are active at once, the
     * highest-priority token (the one listed first) wins, because a single
     * icon slot cannot show two warnings and rotation would pin refreshes at
     * the minimum-interval floor.
     */
    enum class WarningToken : uint8_t {
        NONE,     // margin blank
        OVERHEAT, // over-temperature safety shutoff engaged
        FROST,    // temperature at/below the configured frost threshold
        SENSOR,   // sensor snapshot invalid — explains the value placeholders
        ACTUATOR, // control state UNCERTAIN — the device cannot vouch for the valve
        HUMID     // humidity at/above the configured limit
    };

    // Threshold hysteresis lives here rather than in config: the config stores
    // one value per threshold, the policy adds the release margin — same split
    // as safety_max_c / safety_hyst_c, except the margins are not configurable
    // because a mis-typed release margin is not a user decision worth an NVS
    // key.
    constexpr float FROST_RELEASE_MARGIN_C = 2.0f;
    constexpr float HUMIDITY_RELEASE_MARGIN_PCT = 3.0f;

    // A warning clears only after its condition has been continuously inactive
    // for this long. Onset is never delayed; a lingering warning is harmless,
    // while an icon flapping around a threshold would repaint the panel at the
    // minimum-interval floor forever.
    constexpr uint32_t WARNING_CLEAR_DWELL_S = 30;

    /**
     * The active warning conditions for one evaluation tick.
     *
     * The always-on conditions (overheat, sensorInvalid, actuatorUncertain)
     * cannot be disabled through configuration. The threshold conditions are
     * evaluated from the raw readings; NAN means "no such measurement" and
     * neither fires nor releases a threshold warning.
     */
    struct WarningConditions {
        bool overheat = false;          // TemperatureController::isSafetyShutoffEngaged()
        bool sensorInvalid = false;     // !SensorController::getSnapshot().valid
        bool actuatorUncertain = false; // ControlState::UNCERTAIN
        float temperature = NAN;        // degrees Celsius; NAN = unavailable
        float humidity = NAN;           // percent; NAN = unavailable
    };

    /**
     * Reduces the active conditions to the single warning token the panel
     * should display, with anti-flap state held internally.
     */
    class WarningPolicy {
    public:
        /**
         * @param frostThresholdC     Frost threshold in degrees Celsius; the
         *                            warning fires at or below it. NAN disables.
         * @param humidityThresholdPct Humidity limit in percent; the warning
         *                            fires at or above it. 0 disables.
         */
        WarningPolicy(float frostThresholdC, uint8_t humidityThresholdPct)
            : frostThresholdC(frostThresholdC), humidityThresholdPct(humidityThresholdPct) {}

        /**
         * Both thresholds disabled: the always-on conditions (overheat,
         * sensor, actuator) are still evaluated. Used before begin() hands
         * over the persisted configuration.
         */
        WarningPolicy() : WarningPolicy(NAN, 0) {}

        /**
         * Evaluate the current conditions.
         *
         * Onset is immediate: an active condition becomes the displayed token
         * on the very tick it appears (highest priority wins). Clearance waits
         * for WARNING_CLEAR_DWELL_S of continuous inactivity, measured with
         * wrap-safe unsigned subtraction.
         *
         * @param conditions Active conditions this tick
         * @param nowMs      Monotonic millisecond clock (millis() on the firmware)
         * @return The token the panel should currently display
         */
        WarningToken evaluate(const WarningConditions& conditions, uint32_t nowMs);

        /**
         * Forget all state, as if the device had just booted.
         */
        void reset();

        WarningToken current() const { return displayed; }

        float getFrostThresholdC() const { return frostThresholdC; }
        uint8_t getHumidityThresholdPct() const { return humidityThresholdPct; }

    private:
        float frostThresholdC;        // NAN = disabled
        uint8_t humidityThresholdPct; // 0 = disabled

        WarningToken displayed = WarningToken::NONE;

        // Latches for the hysteretic threshold conditions: fire at the
        // threshold, release past the margin. A NAN reading holds the latch —
        // a missing measurement is not evidence the condition has cleared.
        bool frostLatched = false;
        bool humidityLatched = false;

        // Clear-dwell state: set when the candidate token becomes NONE while
        // something is displayed; the warning clears only once
        // nowMs - inactiveSinceMs reaches the dwell.
        bool clearPending = false;
        uint32_t inactiveSinceMs = 0;
    };

} // namespace Display

#endif // KLIMACONTROL_DISPLAY_WARNINGPOLICY_H
