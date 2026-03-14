#include "SCD4x.h"

namespace Sensor {

    SCD4x::SCD4x(uint8_t address) : I2CSensor(address) {
    }

    bool SCD4x::begin() {
#ifdef ARDUINO
        Serial.println("SCD4x: Initializing sensor...");

        scd.begin(wire, i2cAddress);
        scd.stopPeriodicMeasurement();
        scd.startPeriodicMeasurement();

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading SCD4x::read() {
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        uint16_t co2 = 0;
        float temperature = 0.0f;
        float humidity = 0.0f;
        bool dataReady = false;

        scd.getDataReadyStatus(dataReady);
        if (dataReady && scd.readMeasurement(co2, temperature, humidity) == 0 && co2 > 0) {
            this->co2 = co2;
        }

        if (this->co2 > 0) {
            reading.measurements.push_back({MeasurementType::CO2, static_cast<int32_t>(this->co2), getType(), false});
            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        float t = 22.0f;
        float rh = 45.0f;
        reading.measurements.push_back({MeasurementType::CO2, static_cast<int32_t>(420), getType(), false});

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
