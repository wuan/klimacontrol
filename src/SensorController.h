#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"
#include "StatusLed.h"

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

    void setTargetTemperature(float temperature);
    float getTargetTemperature() const { return config.getDeviceConfig().target_temperature; }

    void setControlEnabled(bool enabled);
    bool isControlEnabled() const { return config.getDeviceConfig().temperature_control_enabled; }

    float updateControl();
    uint32_t getTimeSinceLastReading() const;
    bool hasConnectedSensors() const;

};

#endif // SENSOR_CONTROLLER_H
