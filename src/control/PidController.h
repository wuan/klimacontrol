#ifndef KLIMACONTROL_CONTROL_PIDCONTROLLER_H
#define KLIMACONTROL_CONTROL_PIDCONTROLLER_H

#include <cstdint>

// The temperature control loop's PID algorithm.
//
// Deliberately free of Arduino and FreeRTOS dependencies so the whole loop —
// including its restart behaviour — builds and runs in the `native` PlatformIO
// environment. The caller supplies the clock (`nowMs`) rather than this code
// calling millis(), which is what lets the unit tests drive time directly,
// including across the ~49.7 day rollover.
namespace Control {

    /**
     * Tuning gains. Grouped so a caller cannot transpose two of the three at a
     * call site and still compile.
     */
    struct PidGains {
        float kp;
        float ki;
        float kd;
    };

    /**
     * A PID controller that restarts bumplessly.
     *
     * The interesting part is not the arithmetic but the gap handling. The
     * control loop stops computing whenever control is disabled, sensor data is
     * invalid, or the device has only just booted. If the accumulated state
     * survived those gaps untouched, the first tick afterwards would see a `dt`
     * spanning the entire gap and slam the integral term into its anti-windup
     * clamp — full output regardless of how small the error actually is. An
     * hour switched off would come back heating at maximum with the room a
     * tenth of a degree from target.
     *
     * So the caller reports the gap via suspend(), and the next update() reseats
     * the state before computing. All three fields have to be reseated together:
     * zeroing the integral alone does not help, because the very next statement
     * would re-saturate it from the stale timestamp.
     *
     * Resumption is detected inside update() rather than being pushed by a
     * setter, which keeps this object single-writer. On the firmware, suspend()
     * and update() are both called from the Sensor Monitor task; the web-server
     * task that enables and disables control never touches PID state, so there
     * is no read-modify-write race to guard against.
     */
    class PidController {
    public:
        /**
         * @param gains     Proportional, integral and derivative gains.
         * @param minOutput Lower clamp, applied to both the integral term and
         *                  the final output.
         * @param maxOutput Upper clamp, likewise.
         */
        PidController(PidGains gains, float minOutput, float maxOutput);

        /**
         * Report that the loop did not compute on this tick. The next update()
         * treats itself as a fresh start. Idempotent — a loop that stays
         * suspended for an hour calls this on every tick.
         */
        void suspend();

        /**
         * Advance the controller by one tick and return the clamped output.
         *
         * On the first call after construction or after suspend(), the
         * accumulated state is discarded and the timestamp is reseated to
         * `nowMs`, so `dt` is zero and the result is the clamped proportional
         * term alone. That is the bumpless start: no integral kick from the
         * gap, and no derivative spike from an error measured against a
         * stale sample.
         *
         * @param error Setpoint minus process variable.
         * @param nowMs Monotonic milliseconds. Unsigned arithmetic makes the
         *              elapsed-time calculation correct across rollover.
         */
        float update(float error, uint32_t nowMs);

        /**
         * True when the last tick computed an output, i.e. the next update()
         * will continue rather than restart. Test seam, and useful for logging.
         */
        bool isRunning() const { return running; }

        /**
         * Current integral accumulator. Test seam: anti-windup and bumpless
         * restart are both statements about this value, and asserting on the
         * output alone cannot distinguish "saturated integral" from "large
         * proportional term".
         */
        float getIntegral() const { return integral; }

    private:
        float clamp(float value) const;

        PidGains gains;
        float minOutput;
        float maxOutput;

        float integral = 0.0f;
        float previousError = 0.0f;
        uint32_t lastComputeMs = 0;

        // False until the first completed computation, and again after every
        // suspend(). Drives the reseat in update().
        bool running = false;
    };

}

#endif // KLIMACONTROL_CONTROL_PIDCONTROLLER_H