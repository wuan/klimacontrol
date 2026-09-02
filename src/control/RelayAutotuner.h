#ifndef KLIMACONTROL_CONTROL_RELAYAUTOTUNER_H
#define KLIMACONTROL_CONTROL_RELAYAUTOTUNER_H

#include <cstdint>

#include "control/PidController.h"

// Åström–Hägglund relay autotuning for the temperature control loop.
//
// Deliberately free of Arduino and FreeRTOS dependencies, and the caller
// supplies the clock rather than this code calling millis(). That is what lets
// the whole state machine run in the `native` environment against a simulated
// plant — which matters more here than anywhere else in the codebase, because a
// real run on an underfloor system takes six to twenty hours. Discovering a
// convergence bug at that cadence is not a plan.
//
// This component performs no I/O, persists nothing, and applies nothing. It
// reports a commanded output level and a state; acting on either belongs to the
// caller.
namespace Control {

    enum class AutotuneState : uint8_t {
        Idle,
        Settling,    // waiting for the temperature to stop moving
        Oscillating, // relay switching, peaks being recorded
        Done,        // converged; result() is valid
        Aborted      // abortReason() says why
    };

    enum class AutotuneAbort : uint8_t {
        None,
        UserRequested,
        CeilingBreached,
        FloorBreached,
        SensorLost,
        RunTimeout,          // never converged within the budget
        SettlingTimeout,     // never stopped moving
        AmplitudeTooSmall,   // oscillation never cleared the hysteresis band
        DerivedGainsInvalid  // arithmetic produced something unusable
    };

    /**
     * Bounds and tuning knobs for a run. The defaults suit an underfloor
     * heating plant; every one of them is a safety or termination condition
     * rather than a preference.
     */
    struct AutotuneLimits {
        // Switching band, in Kelvin. Must be several times the sensor's
        // resolution or noise alone will toggle the relay.
        float hysteresis = 0.15f;

        // Half the output swing. The relay drives 0..1, so the amplitude about
        // the midpoint is 0.5. Feeds directly into the Ku calculation.
        float relayAmplitude = 0.5f;

        // Absolute bounds relative to the setpoint. The experiment makes the
        // room oscillate on purpose, so it needs tighter bounds than the normal
        // controller's over-temperature shutoff, and independent of it.
        float ceilingOffset = 3.0f;
        float floorOffset = 3.0f;

        // An underfloor loop that has not produced clean cycles in a day is not
        // going to.
        uint32_t maxDurationMs = 24u * 3600u * 1000u;
        uint32_t settlingTimeoutMs = 30u * 60u * 1000u;

        // Temperature is "steady" below this rate of change.
        float settlingRateKPerMin = 0.05f;

        // Two cycles can agree by coincidence while the plant is still
        // settling; three is the usual recommendation.
        uint8_t requiredCycles = 3;

        // The describing-function estimate divides by sqrt(a^2 - h^2), so as
        // the oscillation amplitude approaches the hysteresis band the
        // estimated Ku runs away to infinity. A cycle that barely clears the
        // band therefore yields absurd — but finite, and so superficially
        // valid — gains. Require real clearance instead.
        float minAmplitudeRatio = 1.2f;

        // Convergence tolerances, as fractions.
        float periodTolerance = 0.15f;
        float amplitudeTolerance = 0.20f;
    };

    /**
     * What a converged run identified and derived. `kd` is always zero — see
     * the derivation note in the .cpp.
     */
    struct AutotuneResult {
        float ku = 0.0f;
        float tu = 0.0f; // seconds
        PidGains gains{0.0f, 0.0f, 0.0f};
    };

    /**
     * Drives the output bang-bang around the setpoint and reads the plant's
     * answer out of the resulting limit cycle.
     *
     * Poll update() once per control tick. It returns the level the output
     * should be driven to; nothing else in this class has side effects.
     */
    class RelayAutotuner {
    public:
        explicit RelayAutotuner(AutotuneLimits limits);

        /**
         * Begin a run around `setpoint`. Resets all accumulated state, so a
         * previous Done or Aborted run is discarded.
         */
        void start(float setpoint, uint32_t nowMs);

        /** Abort with `UserRequested`. Safe to call in any state. */
        void cancel();

        /**
         * Advance one tick and return the commanded output level.
         *
         * Returns `1.0` or `0.0` while oscillating and `0.0` in every other
         * state, so a caller that simply drives the returned value can never
         * leave the output on after a run ends. Returning a level rather than a
         * bool keeps this uniform with PidController::update(), so whatever
         * consumes the control output need not care which produced it.
         *
         * @param dataValid False when the sensor has dropped out; aborts the
         *                  run, because a relay experiment with no feedback is
         *                  an uncontrolled output.
         * @param nowMs     Monotonic milliseconds. Unsigned arithmetic keeps
         *                  every interval correct across the ~49.7 day wrap.
         */
        float update(float temperature, bool dataValid, uint32_t nowMs);

        AutotuneState state() const { return runState; }
        AutotuneAbort abortReason() const { return abortCause; }

        /** Meaningful only when state() == Done. */
        const AutotuneResult &result() const { return runResult; }

        /** Convenience for callers that only want the derived gains. */
        PidGains getResultGains() const { return runResult.gains; }

        uint8_t completedCycles() const { return cycleCount; }
        uint32_t elapsedMs(uint32_t nowMs) const;

    private:
        float abortWith(AutotuneAbort reason);
        void beginOscillating(uint32_t nowMs);
        void recordCycle(uint32_t nowMs);
        bool converged() const;
        void finish();

        AutotuneLimits limits;

        AutotuneState runState = AutotuneState::Idle;
        AutotuneAbort abortCause = AutotuneAbort::None;
        AutotuneResult runResult;

        float setpoint = 0.0f;
        uint32_t startMs = 0;
        uint32_t settlingStartMs = 0;

        // Settling: rate of change is measured between successive ticks rather
        // than smoothed, which is adequate because the threshold is far above
        // per-tick sensor noise once expressed per minute.
        bool haveSettlingSample = false;
        float lastSettlingTemp = 0.0f;
        uint32_t lastSettlingMs = 0;

        // Relay state. Starts on: a run begins below setpoint more often than
        // not, and the first transition is discarded anyway.
        bool relayOn = true;

        // Current cycle, bounded by successive ON transitions.
        bool haveCycleStart = false;
        uint32_t cycleStartMs = 0;
        float cycleMax = 0.0f;
        float cycleMin = 0.0f;

        // Last two completed cycles, for the convergence comparison.
        uint8_t cycleCount = 0;
        float lastPeriodS = 0.0f;
        float lastAmplitude = 0.0f;
        uint8_t consistentCycles = 0;

        // Running means over the consistent run, used for the final result.
        float periodSum = 0.0f;
        float amplitudeSum = 0.0f;
        uint8_t summedCycles = 0;
    };

}

#endif // KLIMACONTROL_CONTROL_RELAYAUTOTUNER_H
