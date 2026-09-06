#ifndef KLIMACONTROL_CONTROL_TEMPERATURECONTROLLER_H
#define KLIMACONTROL_CONTROL_TEMPERATURECONTROLLER_H

#include <atomic>
#include <cstdint>

#include "Config.h"
#include "control/PidController.h"
#include "control/RelayAutotuner.h"
#include "actuator/HeatingActuator.h"

namespace Control {

    /**
     * Temperature Controller - the heating control loop.
     *
     * Owns the PID, the relay autotuner, the over-temperature shutoff latch and
     * the actuator-state publication. It holds no reference to any sensor: the
     * process value is pushed in by the Sensor Monitor task on every tick via
     * update(temperature, valid, nowMs), and the clock is a parameter for the
     * same reason it is on PidController and RelayAutotuner — so the whole loop,
     * including its cadence, runs in the native build.
     *
     * Single-writer: every member below is mutated only from the Sensor Monitor
     * task inside update(), with two documented exceptions that are each
     * single-writer from another task — publishActuatorState() (Network task)
     * and the request flags (AsyncTCP web task). Other tasks read plain scalars,
     * which is safe on the single-core ESP32-S2 without a lock.
     */
    class TemperatureController {
    private:
        Config::ConfigManager &config;

        float lastControlOutput = 0.0f;

        // Owns the PID accumulators. An instance member rather than the
        // function-local statics this used to keep, which were shared by every
        // controller in the process — harmless on the firmware with its
        // single instance, but it leaked state between cases in the native tests.
        //
        // Written only by update(), i.e. only from the Sensor Monitor task.
        // setControlEnabled() runs on the web-server task and deliberately does not
        // touch it; see PidController's class comment.
        Control::PidController pid;

        // Relay autotuning. Like the PID accumulators, this is mutated only by the
        // control-loop task; the web task asks by setting a flag below rather than
        // calling into the state machine mid-update.
        Control::RelayAutotuner autotuner;

        // When the PID last computed, for the decimation in update().
        //
        // Time-based rather than a tick count, because the SensorMonitor task tick
        // is itself settable (SensorMonitor::setReadingInterval): counting every
        // Nth tick would make the configured "60 seconds" mean whatever 60 ticks
        // happened to be. Unsigned, so `now - lastPidComputeMs` stays correct
        // across the millis() rollover.
        uint32_t lastPidComputeMs = 0;

        /**
         * Mark a tick on which the PID deliberately did not compute, and reseat the
         * decimation baseline with it.
         *
         * The two belong together at every skip path: a resumed controller that
         * kept a baseline from before the gap would compute against an interval it
         * never actually waited. Note this is *not* called on a merely decimated
         * tick — see update(), where suspending would reset the integral on
         * every computation and there would be no integral action at all.
         */
        void suspendPid(uint32_t nowMs);

        // Cross-task requests. exchange() makes consumption atomic, so a request
        // cannot be serviced twice, and the loop consumes cancel before start so a
        // pair arriving in the same tick resolves deterministically rather than by
        // arrival order.
        std::atomic<bool> autotuneStartRequested{false};
        std::atomic<bool> autotuneCancelRequested{false};

        // A pending gain change, following the same pattern for the same reason.
        //
        // The web task used to call pid.setGains() directly from
        // acceptAutotuneResult(), which raced the control task inside pid.update():
        // a `running = false` written here could be overwritten by the `running =
        // true` an in-flight update() writes on its way out, dropping the suspend
        // and carrying an integral accumulated under the old gains into the new
        // ones — exactly what setGains() exists to prevent. Three floats can also
        // be read as a mix of old and new.
        //
        // So the gains travel in these plain fields and the flag publishes them:
        // the producer writes the floats and then sets the flag with release
        // ordering, the consumer acquires the flag and then reads the floats, which
        // makes the set indivisible from the control task's point of view. Both
        // entry points (requestGains() and acceptAutotuneResult()) run on the
        // AsyncTCP task, so there is a single producer and no producer-producer
        // race to order.
        std::atomic<bool> gainsChangeRequested{false};
        Control::PidGains pendingGains{0.0f, 0.0f, 0.0f};

        // Latched over-temperature state; see isSafetyShutoffEngaged().
        bool safetyShutoff = false;

        // Whether the most recent update() was handed a usable reading
        // (`valid && !isnan(temperature)`). isHeatingPermitted() answers from
        // this rather than from a live sensor read, so it is at most one Sensor
        // Monitor tick stale and false before the first tick — the safe default.
        bool lastInputValid = false;

        // Actuator state, written only by the Network task via
        // publishActuatorState() and read by the API and the displays.
        bool actuatorAssigned = false;
        Actuator::Agreement actuatorAgreement = Actuator::Agreement::Unknown;

    public:
        /**
         * @param config Configuration manager reference. Setpoint, enable flag,
         *               gains, control interval and safety limits all live in
         *               DeviceConfig; this object is a global constructed before
         *               config.begin() has read NVS, so the constructor sees the
         *               compiled-in defaults and begin() adopts the stored tuning.
         */
        explicit TemperatureController(Config::ConfigManager &config);

        TemperatureController(const TemperatureController &) = delete;
        TemperatureController &operator=(const TemperatureController &) = delete;

        /**
         * Adopt the stored tuning. Called from setup() before the Sensor
         * Monitor task exists, so writing PID state directly is safe — there is
         * no other task to race.
         */
        void begin();

        void setTargetTemperature(float temperature);
        float getTargetTemperature() const { return config.getDeviceConfig().target_temperature; }

        void setControlEnabled(bool enabled);
        bool isControlEnabled() const { return config.getDeviceConfig().temperature_control_enabled; }
        /**
         * Whether heat is confirmed to be going into the room.
         *
         * With a channel assigned this reflects the actuator's observed state, not
         * this controller's intent: during a manifold outage it reports unknown
         * rather than claiming to heat. With no channel assigned it falls back to
         * demand, because nothing exists to confirm against.
         */
        bool isControlActive() const {
            return getReportedState() == Actuator::ReportedState::Heating;
        }

        /** The full picture, for displays that can show more than on/off. */
        Actuator::ReportedState getReportedState() const {
            return Actuator::reportedState(config.getDeviceConfig().temperature_control_enabled,
                                           actuatorAssigned, actuatorAgreement, lastControlOutput);
        }

        /**
         * Published by the Network task after each actuator tick.
         *
         * Cross-task, but single-writer and only ever plain scalars, matching the
         * rule already used for lastControlOutput. The control loop never writes
         * these and the actuator never writes anything else here.
         */
        void publishActuatorState(bool assigned, Actuator::Agreement agreement) {
            actuatorAssigned = assigned;
            actuatorAgreement = agreement;
        }

        // Read-only view of the control loop, for GET /api/control.
        //
        // No locking. These are single 32-bit reads of members written only by the
        // Sensor Monitor task on a single-core part, so a torn read is not
        // possible; isControlActive() above already reads lastControlOutput from
        // the web task the same way. A value that is one tick stale is exactly what
        // a diagnostic view should show.
        float getControlOutput() const { return lastControlOutput; }
        Control::PidGains getControlGains() const { return pid.getGains(); }
        float getControlIntegral() const { return pid.getIntegral(); }
        bool isControlRunning() const { return pid.isRunning(); }
        static constexpr float getControlOutputMin() { return Control::DEFAULT_MIN_OUTPUT; }
        static constexpr float getControlOutputMax() { return Control::DEFAULT_MAX_OUTPUT; }

        /**
         * Ask the control loop to begin an autotune run on its next tick. Returns
         * false when the request cannot be honoured — control disabled, or a run
         * already active. The loop re-checks both; this is the early, friendly
         * refusal so the API can answer 409 immediately.
         */
        bool requestAutotuneStart();

        /** Ask the control loop to cancel any active run. */
        void requestAutotuneCancel();

        /**
         * Persist a converged autotune result and ask the control loop to adopt it
         * on its next tick. Returns false when no converged result exists.
         *
         * Persistence precedes the request, so the gains a caller is told were
         * accepted are the ones that will survive a restart. Success therefore
         * means "validated and accepted", not "already in force" — the control task
         * applies them on a later tick, and getControlGains() is how a caller
         * confirms it did.
         */
        bool acceptAutotuneResult();

        /**
         * Persist a tuning and ask the control loop to adopt it, as
         * acceptAutotuneResult() does but for a hand-entered set. The values are
         * expected to have been validated by the caller: this stores what it is
         * given, and ConfigManager::updateTuning() falls back per field for
         * anything untrustworthy rather than rejecting it.
         */
        void requestGains(Control::PidGains gains, uint16_t controlIntervalS);

        Control::AutotuneState getAutotuneState() const { return autotuner.state(); }
        Control::AutotuneAbort getAutotuneAbort() const { return autotuner.abortReason(); }
        const Control::AutotuneResult &getAutotuneResult() const { return autotuner.result(); }
        uint8_t getAutotuneCycles() const { return autotuner.completedCycles(); }
        uint32_t getAutotuneElapsedMs(uint32_t nowMs) const { return autotuner.elapsedMs(nowMs); }
        bool isAutotuneActive() const {
            return autotuner.state() == Control::AutotuneState::Settling ||
                   autotuner.state() == Control::AutotuneState::Oscillating;
        }

        /**
         * True while the over-temperature shutoff is engaged. Latching: it releases
         * only once the temperature has fallen a hysteresis band below the limit,
         * so the valve does not chatter at the threshold.
         */
        bool isSafetyShutoffEngaged() const { return safetyShutoff; }

        /**
         * Whether the actuator may drive the valve at all. False when control is
         * disabled, the shutoff is engaged, or the last tick had no valid reading —
         * an unknown temperature is not a safe basis for delivering heat.
         *
         * Answered from the inputs of the most recent update(), so at most one
         * Sensor Monitor tick stale, and false before the first tick after boot.
         */
        bool isHeatingPermitted() const;

        /**
         * One control tick. Called by the Sensor Monitor task on every sensor
         * read cycle, unconditionally: it gates on the enable flag and on
         * `valid` itself, and has to run even while disabled so it can mark the
         * tick as skipped for the bumpless restart.
         *
         * @param temperature The current temperature, or NAN when none is available.
         * @param valid       The sensor cache's validity flag, from the same
         *                    getProcessValue() call as `temperature`.
         * @param nowMs       The caller's monotonic clock (millis() on the firmware).
         * @return The effective control output in [getControlOutputMin(), getControlOutputMax()].
         */
        float update(float temperature, bool valid, uint32_t nowMs);
    };

}

#endif // KLIMACONTROL_CONTROL_TEMPERATURECONTROLLER_H
