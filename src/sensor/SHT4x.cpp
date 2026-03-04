#include "SHT4x.h"

namespace Sensor {
    SHT4x::SHT4x(uint8_t address) : I2CSensor(address) {
#ifdef ARDUINO
        sht4x = Adafruit_SHT4x();
#endif
    }

    bool SHT4x::begin() {
#ifdef ARDUINO
        if (!sht4x.begin(&wire)) {
            return false;
        }

        // Set high precision mode
        sht4x.setPrecision(SHT4X_HIGH_PRECISION);
        sht4x.setHeater(SHT4X_NO_HEATER);

        initialized = true;
        return true;
#else
        // For native testing, just mark as initialized
        initialized = true;
        return true;
#endif
    }

    SensorReading SHT4x::read() {
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        sensors_event_t humidity, temp;

        if (sht4x.getEvent(&humidity, &temp)) {
            float temperature = temp.temperature;
            float relativeHumidity = humidity.relative_humidity;
            if (temperature > -30.0f && temperature < 80.0f && relativeHumidity > 5.0f && relativeHumidity < 100.0f) {
                reading.measurements.push_back({MeasurementType::Temperature, temperature, getType(), false});
                reading.measurements.push_back({MeasurementType::RelativeHumidity, relativeHumidity, getType(), false});
                reading.measurements.push_back({
                    MeasurementType::DewPoint, calcDewPoint(temperature, relativeHumidity), getType(), true
                });
                reading.valid = true;
            } else {
                reading.valid = false;
            }
        } else {
            reading.valid = false;
        }
#else
        // For native testing, return some dummy values
        float t = 22.5f;
        float rh = 45.0f;
        reading.measurements.push_back({MeasurementType::Temperature, t, getType(), false});
        reading.measurements.push_back({MeasurementType::RelativeHumidity, rh, getType(), false});

        reading.measurements.push_back({MeasurementType::DewPoint, calcDewPoint(t, rh), getType(), true});

        reading.valid = true;
#endif

        return reading;
    }
} // namespace Sensor
