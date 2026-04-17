#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"

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

    void sortSensors();

    // Thread-safe measurement value accessors (return default on mutex timeout or missing data)
    float getFloatMeasurement(Sensor::MeasurementType type) const;
    int32_t getIntMeasurement(Sensor::MeasurementType type) const;

    // Temperature control state
    uint32_t lastReadingTime;

public:
    explicit SensorController(Config::ConfigManager &config);

    // Delete copy constructor and assignment operator
    SensorController(const SensorController &) = delete;
    SensorController &operator=(const SensorController &) = delete;

    void begin();
    void addSensor(std::unique_ptr<Sensor::Sensor> sensor);
    void readSensors();

    /**
     * Get all current measurements
     */
    std::vector<Sensor::Measurement> getMeasurements() const;

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
