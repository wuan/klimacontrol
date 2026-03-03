#include "BH1750.h"

namespace Sensor {

    BH1750Sensor::BH1750Sensor(uint8_t address) : I2CSensor(address) {
    }

    bool BH1750Sensor::begin() {
#ifdef ARDUINO
        if (!bh.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, i2cAddress, &wire)) {
            return false;
        }
        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading BH1750Sensor::read() {
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        if (bh.measurementReady()) {
            float lux = bh.readLightLevel();
            if (lux >= 0.0f) {
                reading.measurements.push_back({MeasurementType::Illuminance, lux, getType(), false});
                reading.valid = true;
            } else {
                reading.valid = false;
            }
        } else {
            reading.valid = false;
        }
#else
        reading.measurements.push_back({MeasurementType::Illuminance, 250.0f, getType(), false});
        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
