#include "SGP40.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {

    SGP40::SGP40(uint8_t address) : I2CSensor(address) {
    }

    bool SGP40::begin() {
#ifdef ARDUINO
        Serial.println("SGP40: Initializing sensor...");

        if (!sgp.begin(&Wire1)) {
            Serial.println("SGP40: Failed to initialize sensor");
            return false;
        }

        initialized = true;
        Serial.println("SGP40: Sensor initialized successfully");
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading SGP40::read() {
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        int32_t vocIndex = sgp.measureVocIndex();
        if (vocIndex >= 0) {
            reading.measurements.push_back({"voc index", vocIndex, "", "SGP40", false});
            reading.valid = true;
        } else {
            reading.valid = false;
            Serial.println("SGP40: Failed to read sensor data");
        }
#else
        reading.measurements.push_back({"voc index", (int32_t)100, "", "SGP40", false});
        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
