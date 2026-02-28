#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"

class Network;

namespace Sensor {
    class Sensor;
}

/**
 * Sensor Controller - Manages sensors and temperature control
 */
class SensorController {
private:
    Config::ConfigManager &config;
    Network *network; // Pointer to network for status LED control
    std::vector<std::unique_ptr<Sensor::Sensor>> sensors;
    std::vector<Sensor::Measurement> currentMeasurements;
    uint32_t lastReadingTimestamp;
    bool dataValid;

    // Temperature control state
    float targetTemperature;
    bool controlEnabled;
    uint32_t lastReadingTime;

public:
    explicit SensorController(Config::ConfigManager &config);

    void setNetwork(Network *network);

    // Delete copy constructor and assignment operator
    SensorController(const SensorController &) = delete;
    SensorController &operator=(const SensorController &) = delete;

    void begin();
    void addSensor(std::unique_ptr<Sensor::Sensor> sensor);
    void readSensors();

    /**
     * Get all current measurements
     */
    const std::vector<Sensor::Measurement> &getMeasurements() const { return currentMeasurements; }

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
    uint32_t getLastReadingTimestamp() const { return lastReadingTimestamp; }

    /**
     * Whether current data is valid
     */
    bool isDataValid() const { return dataValid; }

    size_t getSensorCount() const { return sensors.size(); }
    Sensor::Sensor *getSensor(size_t index);

    void setTargetTemperature(float temperature);
    float getTargetTemperature() const { return targetTemperature; }

    void setControlEnabled(bool enabled);
    bool isControlEnabled() const { return controlEnabled; }

    float updateControl();
    uint32_t getTimeSinceLastReading() const;
    bool hasConnectedSensors() const;

    void setStatusLedMeasuring();
    void setStatusLedNormal();

};

#endif // SENSOR_CONTROLLER_H
