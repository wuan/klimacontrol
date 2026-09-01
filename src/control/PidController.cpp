#include "control/PidController.h"

#include <algorithm>

namespace Control {

    PidController::PidController(PidGains gains, float minOutput, float maxOutput)
        : gains(gains), minOutput(minOutput), maxOutput(maxOutput) {}

    float PidController::clamp(float value) const {
        return std::max(minOutput, std::min(maxOutput, value));
    }

    void PidController::suspend() {
        running = false;
    }

    float PidController::update(float error, uint32_t nowMs) {
        if (!running) {
            // Bumpless restart. Reseating the timestamp is the part that
            // matters most: without it the gap since the last computation
            // becomes dt and the integral saturates on this very tick.
            integral = 0.0f;
            previousError = 0.0f;
            lastComputeMs = nowMs;
            running = true;
        }

        // Unsigned subtraction so the elapsed time stays correct across the
        // millis() rollover at ~49.7 days.
        const float dt = static_cast<float>(nowMs - lastComputeMs) / 1000.0f;
        lastComputeMs = nowMs;

        const float proportional = gains.kp * error;

        // Anti-windup: the accumulator is clamped to the output range, so a
        // persistent large error cannot bank output it will have to unwind.
        integral = clamp(integral + gains.ki * error * dt);

        // Guarded because a restarted tick has dt == 0. On that tick
        // previousError is zero too, but the guard is what keeps the division
        // defined; the zero previousError only matters from the next tick on,
        // by which point this call has recorded the real error below.
        float derivative = 0.0f;
        if (dt > 0.0f) {
            derivative = gains.kd * (error - previousError) / dt;
        }
        previousError = error;

        return clamp(proportional + integral + derivative);
    }

}