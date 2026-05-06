#include "SCD4x.h"
#include "Log.h"

namespace Sensor {

    SCD4x::SCD4x(uint8_t address) : I2CSensor(address) {
    }

    bool SCD4x::begin() {
#ifdef ARDUINO
        ESP_LOGI("sensor", "SCD4x: Initializing sensor...");

        scd.begin(wire, i2cAddress);

        // Stop any existing periodic measurement. The sensor may already be idle
        // (fresh power-up), so a non-zero error here is tolerable — log and proceed.
        if (int16_t err = scd.stopPeriodicMeasurement(); err != 0) {
            ESP_LOGD("sensor", "SCD4x: stopPeriodicMeasurement returned 0x%04X (ok if sensor was idle)", err);
        }

        // Datasheet: max command duration after stop_periodic_measurement is 500 ms.
        // Skipping this delay can make the following start command fail on cold boots.
        delay(500);

        // start_periodic_measurement must succeed — without it the sensor never
        // produces data and read() would silently return "not ready" forever.
        if (int16_t err = scd.startPeriodicMeasurement(); err != 0) {
            ESP_LOGE("sensor", "SCD4x: startPeriodicMeasurement failed (0x%04X)", err);
            return false;
        }

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading SCD4x::read(const ReadConfig& config, const std::vector<Measurement>& prior) {
        (void) config;
        (void) prior;
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        uint16_t co2 = 0;
        float temperature = 0.0f;
        float humidity = 0.0f;
        bool dataReady = false;

        if (int16_t err = scd.getDataReadyStatus(dataReady); err != 0) {
            ESP_LOGW("sensor", "SCD4x: getDataReadyStatus failed (0x%04X)", err);
            reading.valid = false;
            return reading;
        }

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
        reading.measurements.push_back({MeasurementType::CO2, static_cast<int32_t>(420), getType(), false});

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
