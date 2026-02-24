#ifndef SENSOR_CONTROLLER_H
#define SENSOR_CONTROLLER_H

#include <memory>
#include <vector>
#include "sensor/Sensor.h"
#include "Config.h"

class SensorDataLogger;
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
    Sensor::SensorData currentData;
    
    // Temperature control state
    float targetTemperature;
    bool controlEnabled;
    uint32_t lastReadingTime;
    
    // Data logging
    std::unique_ptr<SensorDataLogger> dataLogger;
    uint32_t lastLogTime;
    uint32_t logInterval;
    
public:
    /**
     * Constructor
     * @param config Configuration manager reference
     */
    explicit SensorController(Config::ConfigManager &config);
    
    /**
     * Set network pointer for status LED control
     * @param network Pointer to network instance
     */
    void setNetwork(Network *network);
    
    // Delete copy constructor and assignment operator
    SensorController(const SensorController &) = delete;
    SensorController &operator=(const SensorController &) = delete;
    
    /**
     * Initialize the sensor controller
     */
    void begin();
    
    /**
     * Add a sensor to the controller
     * @param sensor Unique pointer to sensor instance
     */
    void addSensor(std::unique_ptr<Sensor::Sensor> sensor);
    
    /**
     * Read all sensors and update current data
     */
    void readSensors();
    
    /**
     * Get current sensor data
     * @return Current sensor data
     */
    const Sensor::SensorData &getCurrentData() const { return currentData; }
    
    /**
     * Get number of sensors
     * @return Number of sensors
     */
    size_t getSensorCount() const { return sensors.size(); }
    
    /**
     * Get sensor by index
     * @param index Sensor index
     * @return Pointer to sensor, or nullptr if invalid index
     */
    Sensor::Sensor *getSensor(size_t index);
    
    /**
     * Set target temperature for control
     * @param temperature Target temperature in °C
     */
    void setTargetTemperature(float temperature);
    
    /**
     * Get target temperature
     * @return Target temperature in °C
     */
    float getTargetTemperature() const { return targetTemperature; }
    
    /**
     * Enable/disable temperature control
     * @param enabled True to enable control
     */
    void setControlEnabled(bool enabled);
    
    /**
     * Check if temperature control is enabled
     * @return True if control is enabled
     */
    bool isControlEnabled() const { return controlEnabled; }
    
    /**
     * Update temperature control (PID algorithm)
     * @return Control output (0.0 to 1.0 for heating/cooling)
     */
    float updateControl();
    
    /**
     * Get time since last reading in milliseconds
     * @return Time since last reading
     */
    uint32_t getTimeSinceLastReading() const;
    
    /**
     * Check if any sensor is connected
     * @return True if at least one sensor is connected
     */
    bool hasConnectedSensors() const;
    
    /**
     * Set status LED to measuring state (yellow)
     */
    void setStatusLedMeasuring();
    
    /**
     * Set status LED to normal state (green)
     */
    void setStatusLedNormal();
    
    /**
     * Get data logger
     * @return Pointer to data logger
     */
    SensorDataLogger* getDataLogger() const { return dataLogger.get(); }
    
    /**
     * Set logging interval
     * @param intervalMs Interval in milliseconds
     */
    void setLogInterval(uint32_t intervalMs) { logInterval = intervalMs; }
    
    /**
     * Get logging interval
     * @return Current logging interval in milliseconds
     */
    uint32_t getLogInterval() const { return logInterval; }
};

#endif // SENSOR_CONTROLLER_H