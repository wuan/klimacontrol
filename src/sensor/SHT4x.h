#ifndef SHT4X_H
#define SHT4X_H

#include "Sensor.h"

#ifdef ARDUINO
#include <Adafruit_SHT4x.h>
#endif

namespace Sensor {
    
    /**
     * SHT4x temperature and humidity sensor implementation
     */
    class SHT4x : public Sensor {
    private:
        uint8_t i2cAddress;
        
#ifdef ARDUINO
        Adafruit_SHT4x sht4x;
#endif
        bool initialized;
        
    public:
        /**
         * Constructor
         * @param address I2C address (default: 0x44)
         */
        explicit SHT4x(uint8_t address = 0x44);
        
        bool begin() override;
        SensorReading read() override;
        const char* getName() const override;
        const char* getType() const override;
        bool isConnected() override;
        
        /**
         * Factory for creating SHT4x sensors
         */
        class Factory : public SensorFactory {
            uint8_t address;
            
        public:
            explicit Factory(uint8_t address = 0x44) : address(address) {}
            
            std::unique_ptr<Sensor> createSensor() override {
                return std::make_unique<SHT4x>(address);
            }
        };
    };
    
} // namespace Sensor

#endif // SHT4X_H