#include "SHT4x.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {
    
    SHT4x::SHT4x(uint8_t address) : i2cAddress(address), initialized(false) {
#ifdef ARDUINO
        sht4x = Adafruit_SHT4x();
#endif
    }
    
    bool SHT4x::begin() {
#ifdef ARDUINO
        // if (!Wire.begin()) {
        //     Serial.println("SHT4x: Failed to initialize I2C");
        //     return false;
        // }
        
        if (!sht4x.begin()) {
            Serial.println("SHT4x: Failed to initialize sensor");
            return false;
        }
        
        // Set high precision mode
        sht4x.setPrecision(SHT4X_HIGH_PRECISION);
        sht4x.setHeater(SHT4X_NO_HEATER);
        
        initialized = true;
        Serial.println("SHT4x: Sensor initialized successfully");
        return true;
#else
        // For native testing, just mark as initialized
        initialized = true;
        return true;
#endif
    }
    
    SensorData SHT4x::read() {
        SensorData data;
        data.timestamp = millis();
        
        if (!initialized || !isConnected()) {
            data.valid = false;
            return data;
        }
        
#ifdef ARDUINO
        sensors_event_t humidity, temp;
        
        if (sht4x.getEvent(&humidity, &temp)) {
            data.temperature = temp.temperature;
            data.humidity = humidity.relative_humidity;
            data.valid = true;
        } else {
            data.valid = false;
            Serial.println("SHT4x: Failed to read sensor data");
        }
#else
        // For native testing, return some dummy values
        data.temperature = 22.5f;
        data.humidity = 45.0f;
        data.valid = true;
#endif
        
        return data;
    }
    
    const char* SHT4x::getName() const {
        return "SHT4x";
    }
    
    const char* SHT4x::getType() const {
        return "Temperature/Humidity";
    }
    
    bool SHT4x::isConnected() {
#ifdef ARDUINO
        if (!initialized) return false;
        
        // Try to read the serial number to check connection
        uint32_t serial = sht4x.readSerial();
        return serial != 0;
#else
        return initialized;
#endif
    }
    
} // namespace Sensor