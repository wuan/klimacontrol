#include "DPS310.h"

namespace Sensor {

    DPS310::DPS310(uint8_t address) : I2CSensor(address) {
    }

    bool DPS310::begin() {
#ifdef ARDUINO
        if (!dps.begin_I2C(i2cAddress, &wire)) {
            return false;
        }

        dps.configurePressure(DPS310_64HZ, DPS310_64SAMPLES);
        dps.configureTemperature(DPS310_64HZ, DPS310_64SAMPLES);

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading DPS310::read() {
        return read({}, {});
    }

    SensorReading DPS310::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) prior;
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        sensors_event_t tempEvent, pressureEvent;
        if (dps.temperatureAvailable() && dps.pressureAvailable()) {
            dps.getEvents(&tempEvent, &pressureEvent);
            float pressure = pressureEvent.pressure;
            reading.measurements.push_back({MeasurementType::Pressure, pressure, getType(), false});

            if (config.elevation > 0.0f) {
                float seaLevel = calcSeaLevelPressure(pressure, tempEvent.temperature, config.elevation);
                reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
            }

            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        float stationPressure = 1013.25f;
        float temperature = 22.0f;
        reading.measurements.push_back({MeasurementType::Pressure, stationPressure, getType(), false});

        if (config.elevation > 0.0f) {
            float seaLevel = calcSeaLevelPressure(stationPressure, temperature, config.elevation);
            reading.measurements.push_back({MeasurementType::SeaLevelPressure, seaLevel, getType(), true});
        }

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
