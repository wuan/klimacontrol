#include "control/RelayAutotuner.h"

#include <cmath>

namespace Control {

    namespace {
        constexpr float PI_F = 3.14159265358979323846f;

        // Tyreus–Luyben, not Ziegler–Nichols. ZN targets quarter-amplitude
        // damping — roughly 50 % overshoot — which is the wrong objective on a
        // plant whose thermal mass cannot be discharged quickly: a concrete
        // floor that overshoots by a degree stays too warm for hours.
        constexpr float TL_GAIN_DIVISOR = 3.2f;
        constexpr float TL_PERIOD_FACTOR = 2.2f;

        constexpr float MS_PER_SECOND = 1000.0f;
        constexpr float MS_PER_MINUTE = 60000.0f;

        // Unsigned subtraction, so an interval spanning the millis() rollover
        // at ~49.7 days is the true elapsed time rather than a ~4.29e6 second
        // jump. Every duration in this file goes through here.
        uint32_t since(uint32_t nowMs, uint32_t thenMs) {
            return nowMs - thenMs;
        }

        bool withinTolerance(float a, float b, float tolerance) {
            const float reference = std::fabs(a) > std::fabs(b) ? std::fabs(a) : std::fabs(b);
            if (reference <= 0.0f) {
                return true;
            }
            return std::fabs(a - b) / reference <= tolerance;
        }
    }

    RelayAutotuner::RelayAutotuner(AutotuneLimits limits) : limits(limits) {}

    void RelayAutotuner::start(float setpoint, uint32_t nowMs) {
        this->setpoint = setpoint;
        runState = AutotuneState::Settling;
        abortCause = AutotuneAbort::None;
        runResult = AutotuneResult{};

        startMs = nowMs;
        settlingStartMs = nowMs;
        haveSettlingSample = false;
        lastSettlingTemp = 0.0f;
        lastSettlingMs = nowMs;

        relayOn = true;
        haveCycleStart = false;
        cycleStartMs = nowMs;
        cycleMax = 0.0f;
        cycleMin = 0.0f;

        cycleCount = 0;
        lastPeriodS = 0.0f;
        lastAmplitude = 0.0f;
        consistentCycles = 0;
        periodSum = 0.0f;
        amplitudeSum = 0.0f;
        summedCycles = 0;
    }

    void RelayAutotuner::cancel() {
        if (runState == AutotuneState::Settling || runState == AutotuneState::Oscillating) {
            abortWith(AutotuneAbort::UserRequested);
        }
    }

    float RelayAutotuner::abortWith(AutotuneAbort reason) {
        runState = AutotuneState::Aborted;
        abortCause = reason;
        relayOn = false;
        return 0.0f;
    }

    uint32_t RelayAutotuner::elapsedMs(uint32_t nowMs) const {
        if (runState == AutotuneState::Idle) {
            return 0;
        }
        return since(nowMs, startMs);
    }

    void RelayAutotuner::beginOscillating(uint32_t nowMs) {
        runState = AutotuneState::Oscillating;
        // The relay starts on, so the first ON transition that bounds a cycle
        // is the *next* one. Until then there is no cycle in progress.
        relayOn = true;
        haveCycleStart = false;
        cycleStartMs = nowMs;
    }

    void RelayAutotuner::recordCycle(uint32_t nowMs) {
        const float periodS = static_cast<float>(since(nowMs, cycleStartMs)) / MS_PER_SECOND;
        const float amplitude = (cycleMax - cycleMin) * 0.5f;

        if (cycleCount > 0 && withinTolerance(periodS, lastPeriodS, limits.periodTolerance) &&
            withinTolerance(amplitude, lastAmplitude, limits.amplitudeTolerance)) {
            // Consistent with its predecessor. The first consistent pair counts
            // as two cycles' worth of evidence, since both took part in it.
            if (consistentCycles == 0) {
                consistentCycles = 2;
                periodSum = lastPeriodS + periodS;
                amplitudeSum = lastAmplitude + amplitude;
                summedCycles = 2;
            } else {
                ++consistentCycles;
                periodSum += periodS;
                amplitudeSum += amplitude;
                ++summedCycles;
            }
        } else {
            // The run has not settled into a limit cycle yet; start counting
            // again rather than averaging a transient into the result.
            consistentCycles = 0;
            periodSum = 0.0f;
            amplitudeSum = 0.0f;
            summedCycles = 0;
        }

        lastPeriodS = periodS;
        lastAmplitude = amplitude;
        ++cycleCount;
    }

    bool RelayAutotuner::converged() const {
        return consistentCycles >= limits.requiredCycles;
    }

    void RelayAutotuner::finish() {
        const float tu = periodSum / static_cast<float>(summedCycles);
        const float a = amplitudeSum / static_cast<float>(summedCycles);
        const float h = limits.hysteresis;

        // Hysteresis-corrected describing-function estimate. The uncorrected
        // 4d/(pi*a) over-estimates Ku, and over-estimating Ku yields tuning
        // that is too aggressive — the expensive direction to err on a plant
        // that cannot shed heat quickly.
        // Guarding on `radicand > 0` alone is not enough. Switching uses a
        // strict comparison, so the recorded peaks always sit outside the band
        // and `a > h` holds for any sequence that oscillates at all — that
        // test can never fire. The real hazard is `a` merely *approaching* `h`:
        // the radicand tends to zero, Ku runs away, and the derived gains come
        // out enormous yet finite, sailing past the validity check below.
        const float radicand = a * a - h * h;
        if (!(a >= h * limits.minAmplitudeRatio) || !(radicand > 0.0f)) {
            abortWith(AutotuneAbort::AmplitudeTooSmall);
            return;
        }

        const float ku = (4.0f * limits.relayAmplitude) / (PI_F * std::sqrt(radicand));

        const float kp = ku / TL_GAIN_DIVISOR;
        const float ti = TL_PERIOD_FACTOR * tu;
        // Kd is zero by design: on a lag-dominant underfloor plant the
        // derivative term amplifies sensor noise and contributes nothing.
        const float ki = (ti > 0.0f) ? (kp / ti) : 0.0f;

        if (!std::isfinite(ku) || !std::isfinite(kp) || !std::isfinite(ki) || !(kp > 0.0f) ||
            !(ki > 0.0f)) {
            abortWith(AutotuneAbort::DerivedGainsInvalid);
            return;
        }

        runResult.ku = ku;
        runResult.tu = tu;
        // Ki is in the same parameterisation PidController consumes
        // (integral += Ki * error * dt), so this drops straight in.
        runResult.gains = PidGains{kp, ki, 0.0f};
        runState = AutotuneState::Done;
        relayOn = false;
    }

    float RelayAutotuner::update(float temperature, bool dataValid, uint32_t nowMs) {
        if (runState != AutotuneState::Settling && runState != AutotuneState::Oscillating) {
            // Idle, Done and Aborted all command nothing. Aborts are terminal:
            // a later tick must not resume measurement.
            return 0.0f;
        }

        if (!dataValid || std::isnan(temperature)) {
            return abortWith(AutotuneAbort::SensorLost);
        }

        if (temperature > setpoint + limits.ceilingOffset) {
            return abortWith(AutotuneAbort::CeilingBreached);
        }
        if (temperature < setpoint - limits.floorOffset) {
            return abortWith(AutotuneAbort::FloorBreached);
        }

        if (runState == AutotuneState::Settling) {
            if (since(nowMs, settlingStartMs) > limits.settlingTimeoutMs) {
                return abortWith(AutotuneAbort::SettlingTimeout);
            }

            if (!haveSettlingSample) {
                haveSettlingSample = true;
                lastSettlingTemp = temperature;
                lastSettlingMs = nowMs;
                return relayOn ? 1.0f : 0.0f;
            }

            const uint32_t deltaMs = since(nowMs, lastSettlingMs);
            if (deltaMs > 0) {
                const float ratePerMin =
                    std::fabs(temperature - lastSettlingTemp) * MS_PER_MINUTE /
                    static_cast<float>(deltaMs);
                lastSettlingTemp = temperature;
                lastSettlingMs = nowMs;

                if (ratePerMin < limits.settlingRateKPerMin) {
                    // A run started while the temperature is still moving
                    // identifies the disturbance, not the plant.
                    beginOscillating(nowMs);
                }
            }

            return relayOn ? 1.0f : 0.0f;
        }

        // Oscillating.
        if (since(nowMs, startMs) > limits.maxDurationMs) {
            return abortWith(AutotuneAbort::RunTimeout);
        }

        if (haveCycleStart) {
            if (temperature > cycleMax) {
                cycleMax = temperature;
            }
            if (temperature < cycleMin) {
                cycleMin = temperature;
            }
        }

        const bool wasOn = relayOn;
        if (relayOn && temperature > setpoint + limits.hysteresis) {
            relayOn = false;
        } else if (!relayOn && temperature < setpoint - limits.hysteresis) {
            relayOn = true;
        }

        const bool turnedOn = !wasOn && relayOn;
        if (turnedOn) {
            if (haveCycleStart) {
                recordCycle(nowMs);
                if (converged()) {
                    finish();
                    return 0.0f;
                }
            }
            // Every ON transition opens the next cycle, including the first,
            // which only establishes the boundary and measures nothing.
            haveCycleStart = true;
            cycleStartMs = nowMs;
            cycleMax = temperature;
            cycleMin = temperature;
        }

        return relayOn ? 1.0f : 0.0f;
    }

}
