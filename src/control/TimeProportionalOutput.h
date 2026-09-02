#ifndef KLIMACONTROL_CONTROL_TIMEPROPORTIONALOUTPUT_H
#define KLIMACONTROL_CONTROL_TIMEPROPORTIONALOUTPUT_H

#include <cstdint>

// Converts a continuous demand into valve open time.
//
// Arduino-free with a caller-supplied clock, like PidController and
// RefreshPolicy, so the whole thing runs in the `native` environment. The
// behaviours worth testing here — latching, the dwell rails, the rollover —
// all take fifteen minutes each to observe on real hardware.
namespace Control {

    /**
     * Slow PWM for an on/off valve.
     *
     * The duty is latched once per cycle rather than tracked continuously.
     * Chasing a moving demand mid-cycle produces switching patterns that no
     * longer correspond to a duty ratio, and the entire point of
     * time-proportional output is that the average heat delivered equals the
     * duty.
     *
     * Demand too small to fill one stroke is not discarded. It accumulates as
     * credit and is spent as a single full-length pulse once it is worth one —
     * so 5 % demand becomes a three-minute pulse every third cycle rather than
     * nothing at all. Without that, every duty below one stroke per cycle
     * collapses to zero, which on this plant is most of the heating season: the
     * loop would hunt between nothing and the minimum instead of settling, and
     * would defeat the tuning underneath it.
     *
     * This lives with the actuator rather than the control loop: the loop
     * answers "how much heat" on a minutes-long cadence, while this answers "is
     * the valve open right now" on a seconds-long one. Keeping them separate
     * means the two cadences never have to agree.
     */
    class TimeProportionalOutput {
    public:
        /**
         * @param cycleMs  Length of one open/closed cycle.
         * @param travelMs How long the actuator takes to complete a stroke.
         *                 Sets the minimum dwell: an interval shorter than this
         *                 leaves the valve somewhere between its end stops,
         *                 delivering an amount of heat unrelated to the duty.
         */
        TimeProportionalOutput(uint32_t cycleMs, uint32_t travelMs);

        /**
         * True when the cycle is long enough for the duty to mean anything.
         *
         * A cycle only a little longer than a stroke cannot represent
         * intermediate duties at all — every cycle would snap to one rail.
         * Four strokes is the point at which the middle of the range starts to
         * behave.
         */
        static bool timingValid(uint32_t cycleMs, uint32_t travelMs);

        void setTiming(uint32_t cycleMs, uint32_t travelMs);

        /**
         * Advance to `nowMs` and return whether the valve should be open.
         *
         * The first call after construction or reset() begins a cycle and
         * latches `demand`. Subsequent calls within that cycle ignore `demand`
         * entirely; it is sampled again at the next boundary.
         */
        bool update(float demand, uint32_t nowMs);

        /** Last value returned by update(). */
        bool isOpen() const { return open; }

        /** What this cycle actually delivers, after snapping and skipping. */
        float latchedDuty() const { return duty; }

        /**
         * Heat asked for but not yet delivered, in milliseconds of open time.
         *
         * Positive when demand has been accumulating below the minimum stroke
         * and is waiting to be spent as one pulse; negative when a cycle was
         * rounded up to fully open and delivered more than was asked for. Test
         * and diagnostic seam.
         */
        float creditMs() const { return credit; }

        /** Cycles begun since construction or reset. Test and diagnostic seam. */
        uint32_t completedCycles() const { return cycles; }

        /**
         * Forget the cycle and close. The next update() starts a fresh cycle
         * and latches immediately, so a controller that stops and restarts does
         * not resume half-way through a stale cycle.
         */
        void reset();

    private:
        void beginCycle(float demand, uint32_t nowMs);

        uint32_t cycleMs;
        uint32_t travelMs;

        bool started = false;
        bool open = false;
        uint32_t cycleStartMs = 0;
        uint32_t openMs = 0; // how much of this cycle is open, after snapping
        float duty = 0.0f;
        uint32_t cycles = 0;

        // Undelivered demand, carried between cycles. See beginCycle().
        float credit = 0.0f;
    };

}

#endif // KLIMACONTROL_CONTROL_TIMEPROPORTIONALOUTPUT_H
