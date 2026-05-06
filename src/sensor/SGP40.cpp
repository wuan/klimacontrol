#include "SGP40.h"

namespace Sensor {

    SGP40::SGP40(uint8_t address) : I2CSensor(address) {
    }

    bool SGP40::begin() {
#ifdef ARDUINO
        if (!sgp.begin(&wire)) {
            return false;
        }

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading SGP40::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) config;
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        int32_t vocIndex = -1;
        auto* temperature = findMeasurement(prior, MeasurementType::Temperature);
        auto* relativeHumidity = findMeasurement(prior, MeasurementType::RelativeHumidity);
        if (temperature && relativeHumidity) {
            const float* tempVal = std::get_if<float>(&temperature->value);
            const float* humidVal = std::get_if<float>(&relativeHumidity->value);
            if (tempVal && humidVal) {
                vocIndex = sgp.measureVocIndex(*tempVal, *humidVal);
            }
        }

        if (vocIndex >= 0) {
            reading.measurements.push_back({MeasurementType::VocIndex, vocIndex, getType(), false});
            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        reading.measurements.push_back({MeasurementType::VocIndex, (int32_t)100, getType(), false});
        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
