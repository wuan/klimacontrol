#ifndef SHT4X_H
#define SHT4X_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_SHT4x.h>
#endif

namespace Sensor {
    
    /**
     * SHT4x temperature and humidity sensor implementation
     */
    class SHT4x : public I2CSensor {
#ifdef ARDUINO
        Adafruit_SHT4x sht4x;
#endif

    public:
        /**
         * Constructor
         * @param address I2C address (default: 0x44)
         */
        explicit SHT4x(uint8_t address = 0x44);
        
        static const char* type() { return "SHT4x"; }
        static const uint8_t* addresses() { static const uint8_t a[] = {0x44, 0x45}; return a; }
        static uint8_t addressCount() { return 2; }

        bool begin() override;
        SensorReading read() override;
        const char* getType() const override { return type(); }

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