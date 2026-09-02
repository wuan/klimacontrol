#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"
#include "StatusLed.h"
#include "control/PidController.h"
#include "control/RelayAutotuner.h"

#include <atomic>

#ifdef ARDUINO
#include <freertos/semphr.h>
#endif

namespace Sensor {
    class Sensor;
}

/**
 * Sensor Controller - Manages sensors and temperature control
 */
class SensorController {
private:
    Config::ConfigManager &config;
    std::vector<std::unique_ptr<Sensor::Sensor>> sensors;
    std::vector<Sensor::Measurement> currentMeasurements;
    uint32_t lastReadingTimestamp;
    bool dataValid;

#ifdef ARDUINO
    mutable SemaphoreHandle_t dataMutex;
#endif
    // Non-owning pointer to the status LED; may be nullptr in native tests.
    // Used on the mutex-creation failure path to surface the error visibly.
    // ARDUINO-only: the failure path is the only consumer.
#ifdef ARDUINO
    StatusLed *statusLed;
#endif

    void sortSensors();

    // Thread-safe measurement value accessors (return default on mutex timeout or missing data)
    float getFloatMeasurement(Sensor::MeasurementType type) const;
    int32_t getIntMeasurement(Sensor::MeasurementType type) const;

    // Temperature control state
    uint32_t lastReadingTime;
    float lastControlOutput = 0.0f;

    // Owns the PID accumulators. An instance member rather than the
    // function-local statics this used to keep, which were shared by every
    // SensorController in the process — harmless on the firmware with its
    // single instance, but it leaked state between cases in the native tests.
    //
    // Written only by updateControl(), i.e. only from the Sensor Monitor task.
    // setControlEnabled() runs on the web-server task and deliberately does not
    // touch it; see PidController's class comment.
    Control::PidController pid;

    // Relay autotuning. Like the PID accumulators, this is mutated only by the
    // control-loop task; the web task asks by setting a flag below rather than
    // calling into the state machine mid-update.
    Control::RelayAutotuner autotuner;

    // Cross-task requests. exchange() makes consumption atomic, so a request
    // cannot be serviced twice, and the loop consumes cancel before start so a
    // pair arriving in the same tick resolves deterministically rather than by
    // arrival order.
    std::atomic<bool> autotuneStartRequested{false};
    std::atomic<bool> autotuneCancelRequested{false};

    // Latched over-temperature state; see isSafetyShutoffEngaged().
    bool safetyShutoff = false;

    // Consecutive read cycles in which I2C sensors are present but none returned
    // valid data. After I2C_RECOVERY_FAILURE_STREAK cycles the bus is assumed
    // wedged and a recovery is attempted. Reset on any valid I2C reading.
    // ARDUINO-only: the I2C recovery path is the only consumer.
#ifdef ARDUINO
    uint8_t consecutiveI2CFailures = 0;
#endif
    static constexpr uint8_t I2C_RECOVERY_FAILURE_STREAK = 3;

public:
    /**
     * @param config Configuration manager reference.
     * @param statusLed Optional pointer to the status LED; may be nullptr
     *                  (e.g. in native unit tests). On the firmware, the
     *                  failure path drives this LED to the ERROR state.
     */
    explicit SensorController(Config::ConfigManager &config, StatusLed *statusLed);

    /**
     * Test-only seam: returns true if the underlying mutex allocation failed
     * (or would have failed under ARDUINO). Lets native tests assert on the
     * failure path without having to call ESP.restart().
     */
    bool didFailMutexInit() const;

    // Delete copy constructor and assignment operator
    SensorController(const SensorController &) = delete;
    SensorController &operator=(const SensorController &) = delete;

    /**
     * Consistent point-in-time view of the measurement data, read under a single
     * lock. Use this instead of combining isDataValid()/getLastReadingTimestamp()/
     * getMeasurements() calls, which each take the lock separately and can observe
     * the SensorMonitor task swapping the data mid-read (TOCTOU).
     */
    struct Snapshot {
        bool valid = false;
        uint32_t timestamp = 0;
        std::vector<Sensor::Measurement> measurements;
    };

    void begin();
    void addSensor(std::unique_ptr<Sensor::Sensor> sensor);

    /**
     * Reserve capacity for the sensor and measurement vectors so the
     * I2C scan loop's `addSensor()` calls do not trigger a reallocation.
     * Must be called from main.cpp (the only place that knows the upper
     * bound) before the first `addSensor()`. After this call, adding
     * up to `n` sensors will not reallocate `sensors`, and accumulating
     * up to `n * MAX_MEASUREMENTS_PER_SENSOR` measurements will not
     * reallocate `currentMeasurements`. See spec `memory-management` →
     * "Vector capacities are reserved at boot" for the contract.
     */
    void reserveSensorSlots(size_t n);

    void readSensors();

    /**
     * Atomically capture {valid, timestamp, measurements} under one lock.
     * Returns a default (invalid, empty) snapshot if the lock times out.
     */
    Snapshot getSnapshot() const;

    /**
     * Get all current measurements
     */
    std::vector<Sensor::Measurement> getMeasurements() const;

    /**
     * Atomically returns measurements only if data is currently valid.
     * Returns an empty vector if data is invalid or the mutex acquisition times out.
     * Use this when validity and the data must be consistent (e.g. to avoid publishing
     * stale data after a sensor read just failed).
     */
    std::vector<Sensor::Measurement> getValidMeasurements() const;

    /**
     * Get current temperature (first temperature measurement found)
     * @return temperature value, or NAN if not available
     */
    float getTemperature() const;

    /**
     * Get current humidity (first humidity measurement found)
     * @return humidity value, or NAN if not available
     */
    float getRelativeHumidity() const;

    float getDewPoint() const;

    /**
     * Get current VOC index (first voc index measurement found)
     * @return VOC index value, or -1 if not available
     */
    int32_t getVocIndex() const;

    /**
     * Get timestamp of last reading
     */
    uint32_t getLastReadingTimestamp() const;

    /**
     * Whether current data is valid
     */
    bool isDataValid() const;

    size_t getSensorCount() const { return sensors.size(); }
    Sensor::Sensor *getSensor(size_t index);

    /**
     * Capacity of the internal sensor-list vector. Used by native tests to
     * assert that `reserveSensorSlots(N)` actually prevents reallocation as
     * sensors are added. Cheap (one inline accessor).
     */
    size_t getSensorsCapacity() const { return sensors.capacity(); }

    /**
     * Capacity of the internal measurement vector. Used by native tests to
     * assert the post-`reserveSensorSlots(N)` capacity matches the documented
     * ceiling (`N * MAX_MEASUREMENTS_PER_SENSOR`).
     */
    size_t getMeasurementsCapacity() const { return currentMeasurements.capacity(); }

    void setTargetTemperature(float temperature);
    float getTargetTemperature() const { return config.getDeviceConfig().target_temperature; }

    void setControlEnabled(bool enabled);
    bool isControlEnabled() const { return config.getDeviceConfig().temperature_control_enabled; }
    bool isControlActive() const { return lastControlOutput > 0.0f; }

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
     * Apply a converged autotune result to the running controller. In memory
     * only — there is no configuration field for gains yet, so this is lost on
     * restart. Returns false when no converged result exists.
     */
    bool acceptAutotuneResult();

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
     * disabled, the shutoff is engaged, or there is no valid reading — an
     * unknown temperature is not a safe basis for delivering heat.
     */
    bool isHeatingPermitted() const;

    float updateControl();
    uint32_t getTimeSinceLastReading() const;
    bool hasConnectedSensors() const;

};

#endif // SENSOR_CONTROLLER_H
