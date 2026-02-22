#ifndef SENSOR_H
#define SENSOR_H

#include <cstdint>
#include <memory>

namespace Sensor {
    
    /**
     * Sensor reading data structure
     */
    struct SensorData {
        float temperature;  // Temperature in °C
        float humidity;     // Relative humidity in %
        uint32_t timestamp;  // Timestamp in milliseconds
        bool valid;          // Whether the reading is valid
        
        SensorData() : temperature(0.0f), humidity(0.0f), timestamp(0), valid(false) {}
    };
    
    /**
     * Base sensor interface
     */
    class Sensor {
    public:
        virtual ~Sensor() = default;
        
        /**
         * Initialize the sensor
         * @return true if initialization succeeded
         */
        virtual bool begin() = 0;
        
        /**
         * Read sensor data
         * @return SensorData structure with current readings
         */
        virtual SensorData read() = 0;
        
        /**
         * Get sensor name
         * @return Sensor name string
         */
        virtual const char* getName() const = 0;
        
        /**
         * Get sensor type
         * @return Sensor type string
         */
        virtual const char* getType() const = 0;
        
        /**
         * Check if sensor is connected and working
         * @return true if sensor is connected
         */
        virtual bool isConnected() = 0;
    };
    
    /**
     * Sensor factory interface for creating sensor instances
     */
    class SensorFactory {
    public:
        virtual ~SensorFactory() = default;
        virtual std::unique_ptr<Sensor> createSensor() = 0;
    };
    
} // namespace Sensor

#endif // SENSOR_H