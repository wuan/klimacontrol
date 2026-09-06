#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"
#include "DarkModeStatusLed.h"

#ifdef ARDUINO
#include <freertos/semphr.h>
#endif

namespace Sensor {
    class Sensor;
}

/**
 * Sensor Controller - Schedules sensor reads and caches the results
 *
 * Purely a sensor concern: the heating control loop that used to live here is
 * Control::TemperatureController, which is fed by the Sensor Monitor task from
 * getProcessValue() and holds no reference to this class.
 *
 * Read scheduling. The SensorMonitor task calls readSensors() once a second,
 * but a sensor is only *read* when it is due. Drivers with no requirement of
 * their own (Sensor::requiredIntervalMs() == 0) are read together on one
 * shared phase every MEASUREMENT_INTERVAL_MS, so temperature, humidity and
 * the values derived from them are always from the same instant. Drivers
 * with a requirement (the SGP40's 1 Hz) keep their own timer.
 *
 * Because most sensors sit out most ticks, each sensor has a cache slot
 * holding its last valid reading. The published snapshot is the union of the
 * valid slots, rebuilt every tick, so a consumer never sees a measurement
 * type disappear just because its sensor was not due. See the
 * `sensor-management` spec, "Per-sensor last-good cache".
 */
class SensorController {
public:
    /**
     * How often default-interval sensors are read. A constant, not a config
     * knob: nothing downstream needs data faster (PID computes every
     * control_interval_s, MQTT publishes every 15 s by default), and reading
     * faster costs real things — the BME680 fires a 150 ms heater per read.
     */
    static constexpr uint32_t MEASUREMENT_INTERVAL_MS = 15000;

    /**
     * How many missed intervals a cached reading survives before it is
     * dropped. Bounds staleness independently of the driver's
     * READ_FAILURE_THRESHOLD, which at 15 s reads would otherwise let a dead
     * sensor's last value feed the PID for 150 s.
     */
    static constexpr uint32_t SLOT_EXPIRY_INTERVALS = 3;

private:
    Config::ConfigManager &config;
    std::vector<std::unique_ptr<Sensor::Sensor>> sensors;
    std::vector<Sensor::Measurement> currentMeasurements;
    uint32_t lastReadingTimestamp;
    bool dataValid;

    // One per sensor, same index as `sensors`. Written only by readSensors()
    // on the SensorMonitor task; readers see the union via currentMeasurements.
    struct SensorSlot {
        std::vector<Sensor::Measurement> measurements; // last valid reading + Time
        uint32_t lastValidMs = 0;   // when `measurements` was taken
        uint32_t lastReadMs = 0;    // when read() was last attempted
        bool everRead = false;      // lastReadMs is meaningful
        bool valid = false;
    };
    std::vector<SensorSlot> slots;

    // The shared phase for default-interval sensors (D2 in the change design).
    // A single controller-wide baseline rather than per-sensor timers, so a
    // sensor that comes online late via the retry path still lands on the
    // same tick as the others.
    uint32_t lastDefaultCycleMs = 0;
    bool defaultCycleRun = false;

    // Effective read interval for a sensor: its own requirement, or the system default.
    static uint32_t effectiveIntervalMs(const Sensor::Sensor &sensor) {
        const uint32_t required = sensor.requiredIntervalMs();
        return required > 0 ? required : MEASUREMENT_INTERVAL_MS;
    }

    // Concatenate every valid slot, in sensor order, into `out`.
    void collectValidSlots(std::vector<Sensor::Measurement> &out) const;

#ifdef ARDUINO
    mutable SemaphoreHandle_t dataMutex;
#endif
    // Non-owning pointer to the status LED; may be nullptr in native tests.
    // Used on the mutex-creation failure path to surface the error visibly.
    // ARDUINO-only: the failure path is the only consumer.
#ifdef ARDUINO
    DarkModeStatusLed *statusLed;
#endif

    void sortSensors();

    // Thread-safe measurement value accessors (return default on mutex timeout or missing data)
    float getFloatMeasurement(Sensor::MeasurementType type) const;
    int32_t getIntMeasurement(Sensor::MeasurementType type) const;

    uint32_t lastReadingTime;

    // Consecutive read cycles in which at least one I2C sensor was *attempted*
    // but none returned valid data. After I2C_RECOVERY_FAILURE_STREAK cycles
    // the bus is assumed wedged and a recovery is attempted. Reset on any valid
    // I2C reading; untouched on ticks where no I2C sensor was due, so the 14
    // quiet ticks per default cycle cannot masquerade as a wedged bus.
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
    explicit SensorController(Config::ConfigManager &config, DarkModeStatusLed *statusLed);

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

    /**
     * The control loop's input, captured under a single lock so temperature
     * and validity describe the same instant. Two scalars and a timestamp
     * rather than a Snapshot, because the loop needs no vector copy — and a
     * heap allocation per second is not free on a device that tracks
     * fragmentation.
     */
    struct ProcessValue {
        float temperature = NAN;    // what getTemperature() would return
        bool valid = false;         // what isDataValid() would return
        uint32_t timestamp = 0;     // what getLastReadingTimestamp() would return
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

    /** Equivalent to readSensors(millis()). */
    void readSensors();

    /**
     * One scheduling tick: read every sensor that is due at `nowMs`, refresh
     * its cache slot, expire stale slots, and publish the union. Takes the
     * clock as a parameter so native tests can drive the schedule without
     * sleeping; the firmware always passes millis().
     */
    void readSensors(uint32_t nowMs);

    /**
     * Atomically capture {valid, timestamp, measurements} under one lock.
     * Returns a default (invalid, empty) snapshot if the lock times out.
     */
    Snapshot getSnapshot() const;

    /**
     * Atomically capture {temperature, valid, timestamp} under one lock, with
     * no allocation. Returns the default (NaN, invalid, 0) if the lock times
     * out, which the control loop already treats as "no data".
     */
    ProcessValue getProcessValue() const;

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

    uint32_t getTimeSinceLastReading() const;
    bool hasConnectedSensors() const;

};

#endif // SENSOR_CONTROLLER_H
