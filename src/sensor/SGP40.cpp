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

    SensorReading SGP40::read() {
        return read({}, {});
    }

    SensorReading SGP40::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) config;
        SensorReading reading;
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

        auto* temperature = findMeasurement(prior, MeasurementType::Temperature);
        auto* relativeHumidity = findMeasurement(prior, MeasurementType::RelativeHumidity);

#ifdef ARDUINO
        int32_t vocIndex = (temperature && relativeHumidity)
            ? sgp.measureVocIndex(std::get<float>(temperature->value), std::get<float>(relativeHumidity->value))
            : sgp.measureVocIndex();

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
