#ifndef KLIMACONTROL_ACTUATOR_HEATINGACTUATOR_H
#define KLIMACONTROL_ACTUATOR_HEATINGACTUATOR_H

#include <cstdint>

#include "Config.h"
#include "actuator/ShellyChannel.h"
#include "control/TimeProportionalOutput.h"

// Drives one Shelly Pro 4PM channel from the controller's demand.
//
// Lives on the Network task, not the control loop. The Sensor Monitor task
// feeds a watchdog every second; an unreachable manifold takes seconds to time
// out, and a blocking HTTP call there would stall sensing. Demand crosses
// between the two as a plain float read — a single aligned 32-bit load on a
// single-core part, the same argument already used for isControlActive().
namespace Actuator {

    /**
     * What the relay is actually doing, as last observed. Distinct from what we
     * commanded: with a remote actuator the command is a belief until the relay
     * confirms it.
     */
    struct Observation {
        bool valid = false;   // false when the manifold has not answered recently
        bool output = false;  // the relay's own view of its contact
        float apower = 0.0f;  // watts; a wax head draws roughly 2-3
        uint32_t atMs = 0;
    };

    /**
     * How the commanded and observed states line up. The interesting value is
     * NoActuator: the relay closed but nothing drew current, which is the only
     * way to detect a dead or disconnected wax head. Without it the valve
     * reports open, the controller believes it is heating, and the room simply
     * never warms.
     */
    enum class Agreement : uint8_t {
        Unknown,     // no recent observation
        ClosedOk,    // commanded off, observed off
        HeatingOk,   // commanded on, observed on, drawing power
        NoActuator,  // commanded on, contact closed, no current
        RelayRefused // commanded on, contact open
    };

    /** Stable machine-readable name for the API. */
    const char *agreementName(Agreement a);

    class HeatingActuator {
    public:
        HeatingActuator();

        /**
         * Apply configuration. Returns false when the device has no usable
         * assignment, in which case the actuator stays inert and control must
         * not be enabled.
         */
        bool configure(const Config::DeviceConfig &config);

        /**
         * One tick, called from the Network task at roughly ACTUATOR_TICK_MS.
         *
         * @param demand    Controller output in [0,1]; negative or NaN is off.
         * @param permitted False when control is disabled, the safety shutoff
         *                  has engaged, or sensor data is invalid. The valve is
         *                  commanded closed and the cycle is reset, so a
         *                  resumed controller does not land mid-cycle.
         */
        void tick(float demand, bool permitted, uint32_t nowMs);

        /** Command the valve closed now, regardless of cycle phase. */
        void forceClosed(uint32_t nowMs);

        /**
         * Re-read the channel configuration on the next tick, rather than
         * waiting out the periodic interval. For after somebody has just fixed
         * a setting in the Shelly's own UI.
         */
        void requestRecheck() { everChecked = false; }

        bool isAssigned() const { return assigned; }
        Conformance conformance() const { return lastConformance; }
        bool isConforming() const { return lastConformance == Conformance::Ok; }
        bool commandedOpen() const { return commanded; }
        const Observation &observation() const { return observed; }
        Agreement agreement(uint32_t nowMs) const;
        float latchedDuty() const { return tpo.latchedDuty(); }
        uint32_t failedRequests() const { return failures; }

        // Renewal must be frequent enough that the relay's lease cannot expire
        // while the valve is meant to be open, and the lease must be wide
        // enough that one failed request does not move a valve that takes
        // minutes per stroke.
        static constexpr uint32_t TICK_MS = 10000;
        static constexpr uint32_t RENEW_MS = 30000;
        static constexpr uint32_t OBSERVE_MS = 30000;
        static constexpr uint32_t CONFORMANCE_RECHECK_MS = 300000;
        static constexpr uint32_t OBSERVATION_STALE_MS = 120000;

        // Below this the relay is closed but nothing is drawing current.
        static constexpr float MIN_ACTUATOR_WATTS = 0.5f;

        // The lease must span several renewals; see design.md.
        static constexpr float MIN_LEASE_S = RENEW_MS / 1000.0f * 4.0f;

    private:
        bool sendSet(bool on);
        bool readStatus(uint32_t nowMs);
        bool readConfig(uint32_t nowMs);

        Control::TimeProportionalOutput tpo;

        char host[64] = "";
        int8_t channel = Config::ACTUATOR_CHANNEL_UNASSIGNED;
        bool assigned = false;

        Conformance lastConformance = Conformance::NotRead;
        uint32_t lastConformanceMs = 0;
        bool everChecked = false;

        bool commanded = false;
        uint32_t lastCommandMs = 0;
        Observation observed;
        uint32_t lastObserveMs = 0;
        uint32_t failures = 0;
    };

}

#endif // KLIMACONTROL_ACTUATOR_HEATINGACTUATOR_H
