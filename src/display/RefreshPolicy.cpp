#include "RefreshPolicy.h"

#include <cmath>
#include <cstdio>

namespace Display {

    namespace {
        // A reading counts as available only if the snapshot is valid AND the
        // value itself is a real number. SensorController's accessors return
        // NAN for "no such measurement", which must render as a placeholder
        // rather than as 0.0.
        bool readingAvailable(float temperature, float humidity, bool valid) {
            return valid && !std::isnan(temperature) && !std::isnan(humidity);
        }

        // The setpoint is rendered with one decimal, so anything below half a
        // digit is invisible. NAN (unknown setpoint) renders as a placeholder,
        // and two placeholders are the same picture.
        bool setpointChanged(float current, float previous) {
            if (std::isnan(current) || std::isnan(previous)) {
                return std::isnan(current) != std::isnan(previous);
            }
            return std::fabs(current - previous) >= 0.05f;
        }
    } // namespace

    RefreshPolicy::RefreshPolicy(uint16_t minIntervalSec)
        : minIntervalSec(minIntervalSec) {
    }

    uint8_t nextDemandBucket(float fraction, uint8_t previous) {
        if (std::isnan(fraction) || fraction <= 0.0f) {
            return 0;
        }
        if (fraction >= 1.0f) {
            return DEMAND_BUCKETS;
        }
        if (previous > DEMAND_BUCKETS) {
            previous = DEMAND_BUCKETS;
        }

        const float width = 1.0f / static_cast<float>(DEMAND_BUCKETS);

        // Move up only once the fraction clears the top of the current bucket
        // by the hysteresis margin, and down only once it drops below the
        // bottom by the same. Between those it holds, so a demand hovering on
        // an edge does not repaint the panel every tick.
        uint8_t bucket = previous;
        while (bucket < DEMAND_BUCKETS &&
               fraction > static_cast<float>(bucket) * width + DEMAND_BUCKET_HYSTERESIS) {
            ++bucket;
        }
        while (bucket > 0 &&
               fraction < static_cast<float>(bucket - 1) * width - DEMAND_BUCKET_HYSTERESIS) {
            --bucket;
        }
        return bucket;
    }

    void RefreshPolicy::reset() {
        everPainted = false;
        lastValid = false;
        lastTemperature = 0.0f;
        lastHumidity = 0.0f;
        lastRefreshMs = 0;
        lastClockMinute = 0;
        lastSetpoint = NAN;
        lastDemandBucket = 0;
        lastControlState = ControlState::INACTIVE;
        partialsSinceFull = 0;
    }

    RefreshKind RefreshPolicy::commit(RefreshKind kind, float temperature, float humidity,
                                      bool valid, uint32_t nowMs, uint32_t clockMinute,
                                      float setpoint, ControlState controlState,
                                      uint8_t demandBucket) {
        everPainted = true;
        lastValid = valid;
        lastTemperature = temperature;
        lastHumidity = humidity;
        lastRefreshMs = nowMs;
        lastClockMinute = clockMinute;
        lastSetpoint = setpoint;
        lastDemandBucket = demandBucket;
        lastControlState = controlState;

        if (kind == RefreshKind::Full) {
            partialsSinceFull = 0;
        } else if (kind == RefreshKind::Partial) {
            partialsSinceFull++;
        }

        return kind;
    }

    RefreshKind RefreshPolicy::evaluate(float temperature, float humidity, bool valid,
                                        uint32_t nowMs, uint32_t clockMinute, float setpoint,
                                        ControlState controlState, uint8_t demandBucket) {
        const bool available = readingAvailable(temperature, humidity, valid);

        // 1. First paint after boot is always a full refresh: the panel may be
        //    holding an arbitrary image from a previous run, since e-paper
        //    retains its contents unpowered.
        if (!everPainted) {
            return commit(RefreshKind::Full, temperature, humidity, available, nowMs, clockMinute,
                          setpoint, controlState, demandBucket);
        }

        // 2. Hysteresis. A validity transition in either direction bypasses the
        //    value check so the panel switches between the reading and the
        //    placeholder promptly. When neither side has a reading there is
        //    nothing to compare and nothing to show that isn't already shown.
        bool changed;
        if (available != lastValid) {
            changed = true;
        } else if (!available) {
            changed = false;
        } else {
            changed = std::fabs(temperature - lastTemperature) >= TEMP_HYSTERESIS_C ||
                      std::fabs(humidity - lastHumidity) >= HUMIDITY_HYSTERESIS_PCT;
        }

        // The footer timestamp is part of what the panel shows, so the minute
        // rolling over is a change in its own right — the display keeps a live
        // clock rather than only a timestamp of the last reading. Also covers
        // the unsynced-to-synced transition, where the blank footer gains a
        // time. The minimum-interval floor below still applies, so this cannot
        // refresh faster than the configured budget allows.
        if (clockMinute != lastClockMinute) {
            changed = true;
        }

        // The setpoint and the control symbol are footer content the user can
        // change from the web UI at any time. Without this they would only
        // appear once a reading happened to move past hysteresis or the minute
        // rolled over — and with NTP unsynced the minute never rolls over, so a
        // stable sensor could hold the stale value indefinitely.
        if (setpointChanged(setpoint, lastSetpoint) || controlState != lastControlState) {
            changed = true;
        }

        // The demand bar is footer content too. It arrives already bucketed and
        // hysteretic (nextDemandBucket), so this is a plain comparison — the
        // whole point of quantising upstream is that the panel only repaints
        // when the bar visibly changes, not on every tick of a moving output.
        if (demandBucket != lastDemandBucket) {
            changed = true;
        }

        if (!changed) {
            return RefreshKind::None;
        }

        // 3. Minimum-interval floor. Unsigned subtraction so the comparison
        //    stays correct across the millis() rollover at ~49.7 days.
        const uint32_t elapsedMs = nowMs - lastRefreshMs;
        const uint32_t minIntervalMs = static_cast<uint32_t>(minIntervalSec) * 1000u;
        if (elapsedMs < minIntervalMs) {
            // Deliberately does NOT record the new values: the change is still
            // outstanding and must fire once the floor passes.
            return RefreshKind::None;
        }

        // 4. Ghosting. Promote every Nth consecutive partial to a full refresh.
        const RefreshKind kind = (partialsSinceFull >= FULL_REFRESH_EVERY_N_PARTIALS)
                                     ? RefreshKind::Full
                                     : RefreshKind::Partial;

        return commit(kind, temperature, humidity, available, nowMs, clockMinute, setpoint,
                      controlState, demandBucket);
    }

    size_t formatTemperature(char *out, size_t n, float value, bool valid) {
        if (out == nullptr || n == 0) {
            return 0;
        }
        if (!valid || std::isnan(value)) {
            const int written = snprintf(out, n, "--.-");
            return written < 0 ? 0 : static_cast<size_t>(written);
        }
        const int written = snprintf(out, n, "%.1f", static_cast<double>(value));
        return written < 0 ? 0 : static_cast<size_t>(written);
    }

    size_t formatHumidity(char *out, size_t n, float value, bool valid) {
        if (out == nullptr || n == 0) {
            return 0;
        }
        if (!valid || std::isnan(value)) {
            const int written = snprintf(out, n, "--");
            return written < 0 ? 0 : static_cast<size_t>(written);
        }
        const int written = snprintf(out, n, "%.0f", static_cast<double>(value));
        return written < 0 ? 0 : static_cast<size_t>(written);
    }

} // namespace Display
