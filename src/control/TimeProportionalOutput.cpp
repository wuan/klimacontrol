#include "control/TimeProportionalOutput.h"

#include <cmath>

namespace Control {

    namespace {
        // The cycle must fit several full strokes or intermediate duties cannot
        // be represented: every cycle would snap to one rail and the controller
        // would degrade to bang-bang without saying so.
        constexpr uint32_t MIN_CYCLES_PER_TRAVEL = 4;

        uint32_t since(uint32_t nowMs, uint32_t thenMs) {
            return nowMs - thenMs; // unsigned: correct across the ~49.7 day wrap
        }
    }

    TimeProportionalOutput::TimeProportionalOutput(uint32_t cycleMs, uint32_t travelMs)
        : cycleMs(cycleMs), travelMs(travelMs) {}

    bool TimeProportionalOutput::timingValid(uint32_t cycleMs, uint32_t travelMs) {
        if (cycleMs == 0) {
            return false;
        }
        return cycleMs >= travelMs * MIN_CYCLES_PER_TRAVEL;
    }

    void TimeProportionalOutput::setTiming(uint32_t cycleMs, uint32_t travelMs) {
        this->cycleMs = cycleMs;
        this->travelMs = travelMs;
        // Timing changed underneath the current cycle, so the cycle it was
        // computed for no longer exists.
        reset();
    }

    void TimeProportionalOutput::reset() {
        started = false;
        open = false;
        openMs = 0;
        duty = 0.0f;
        cycles = 0;
        credit = 0.0f;
    }

    void TimeProportionalOutput::beginCycle(float demand, uint32_t nowMs) {
        cycleStartMs = nowMs;
        started = true;
        ++cycles;

        float clamped = demand;
        if (std::isnan(clamped) || clamped < 0.0f) {
            clamped = 0.0f;
        } else if (clamped > 1.0f) {
            clamped = 1.0f;
        }

        if (clamped <= 0.0f) {
            // Nothing is being asked for, so nothing is owed. Carrying credit
            // across a period of zero demand would deliver stale heat later,
            // long after the reason for it had gone.
            credit = 0.0f;
            openMs = 0;
            duty = 0.0f;
            return;
        }

        const float cycleF = static_cast<float>(cycleMs);
        credit += clamped * cycleF;
        if (credit > cycleF) {
            credit = cycleF; // never bank more than one cycle can spend
        }

        // Dwell snapping. An open interval shorter than a stroke never lifts
        // the valve off its seat; a closed interval shorter than a stroke never
        // lets it seat. Either way the heat delivered bears no relation to the
        // duty.
        //
        // Rather than discarding the shortfall, it stays as credit and is spent
        // once it is worth a full stroke. The rounding up at the other rail
        // pushes credit negative, and the debt is worked off over the following
        // cycles — so the *average* tracks demand even though no individual
        // cycle can.
        uint32_t requested = 0;
        if (credit >= static_cast<float>(travelMs)) {
            requested = (credit >= cycleF) ? cycleMs : static_cast<uint32_t>(credit + 0.5f);
            if (cycleMs - requested < travelMs) {
                requested = cycleMs;
            }
        }

        credit -= static_cast<float>(requested);
        if (credit < -cycleF) {
            credit = -cycleF;
        }

        openMs = requested;
        duty = (cycleMs > 0) ? static_cast<float>(openMs) / static_cast<float>(cycleMs) : 0.0f;
    }

    bool TimeProportionalOutput::update(float demand, uint32_t nowMs) {
        if (!started) {
            beginCycle(demand, nowMs);
        } else if (since(nowMs, cycleStartMs) >= cycleMs) {
            // Start the next cycle from now rather than from cycleStart+cycleMs.
            // The caller's tick will not land exactly on the boundary, and
            // accumulating that remainder would drift the cycle phase.
            beginCycle(demand, nowMs);
        }

        const uint32_t elapsed = since(nowMs, cycleStartMs);
        open = elapsed < openMs;
        return open;
    }

}
