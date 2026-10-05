#include "display/WarningPolicy.h"

#include <cmath>

namespace Display {

    WarningToken WarningPolicy::evaluate(const WarningConditions& conditions, uint32_t nowMs) {
        // --- threshold hysteresis ---
        //
        // Humidity fires at or above the limit and releases below it by
        // HUMIDITY_RELEASE_MARGIN_PCT; frost fires at or below the threshold
        // and releases above it by FROST_RELEASE_MARGIN_C. A reading hovering
        // within the margin therefore holds whatever state it had instead of
        // toggling the icon (and the panel refresh with it) on every tick.

        if (humidityThresholdPct != 0 && !std::isnan(conditions.humidity)) {
            const float threshold = static_cast<float>(humidityThresholdPct);
            if (!humidityLatched) {
                if (conditions.humidity >= threshold) {
                    humidityLatched = true;
                }
            } else if (conditions.humidity < threshold - HUMIDITY_RELEASE_MARGIN_PCT) {
                humidityLatched = false;
            }
        }

        if (!std::isnan(frostThresholdC) && !std::isnan(conditions.temperature)) {
            if (!frostLatched) {
                if (conditions.temperature <= frostThresholdC) {
                    frostLatched = true;
                }
            } else if (conditions.temperature > frostThresholdC + FROST_RELEASE_MARGIN_C) {
                frostLatched = false;
            }
        }

        // --- priority selection: declaration order of WarningToken ---
        WarningToken candidate = WarningToken::NONE;
        if (conditions.overheat) {
            candidate = WarningToken::OVERHEAT;
        } else if (frostLatched) {
            candidate = WarningToken::FROST;
        } else if (conditions.sensorInvalid) {
            candidate = WarningToken::SENSOR;
        } else if (conditions.actuatorUncertain) {
            candidate = WarningToken::ACTUATOR;
        } else if (humidityLatched) {
            candidate = WarningToken::HUMID;
        }

        if (candidate != WarningToken::NONE) {
            // Onset (and a priority change while a warning is up) is immediate:
            // a late warning is the dangerous kind. Clears any pending dwell.
            displayed = candidate;
            clearPending = false;
            return displayed;
        }

        // --- clear dwell ---
        //
        // The condition has gone inactive but a warning is still displayed.
        // It stays up until the condition has been continuously inactive for
        // WARNING_CLEAR_DWELL_S; a brief recovery resets nothing visible but
        // does cancel the pending clear. Unsigned subtraction so the elapsed
        // comparison stays correct across the millis() rollover.
        if (displayed != WarningToken::NONE) {
            if (!clearPending) {
                clearPending = true;
                inactiveSinceMs = nowMs;
            }
            if (nowMs - inactiveSinceMs >= WARNING_CLEAR_DWELL_S * 1000u) {
                displayed = WarningToken::NONE;
                clearPending = false;
            }
        }

        return displayed;
    }

    void WarningPolicy::reset() {
        displayed = WarningToken::NONE;
        frostLatched = false;
        humidityLatched = false;
        clearPending = false;
        inactiveSinceMs = 0;
    }

} // namespace Display
