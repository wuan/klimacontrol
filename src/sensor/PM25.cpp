#include "PM25.h"

namespace Sensor {

    PM25::PM25(uint8_t address) : I2CSensor(address) {
    }

    bool PM25::begin() {
#ifdef ARDUINO
        if (!aqi.begin_I2C(&wire)) {
            return false;
        }

        initialized = true;
        return true;
#else
        initialized = true;
        return true;
#endif
    }

    SensorReading PM25::read() {
        SensorReading reading;
        reading.measurements.reserve(measurementCount());
        reading.timestamp = millis();

        if (!initialized || !isConnected()) {
            reading.valid = false;
            return reading;
        }

#ifdef ARDUINO
        PM25_AQI_Data data;
        if (aqi.read(&data)) {
            reading.measurements.push_back({MeasurementType::Particles03, static_cast<int32_t>(data.particles_03um), getType(), false});
            reading.measurements.push_back({MeasurementType::Particles05, static_cast<int32_t>(data.particles_05um), getType(), false});
            reading.measurements.push_back({MeasurementType::Particles10, static_cast<int32_t>(data.particles_10um), getType(), false});
            reading.measurements.push_back({MeasurementType::Particles25, static_cast<int32_t>(data.particles_25um), getType(), false});
            reading.measurements.push_back({MeasurementType::Particles50, static_cast<int32_t>(data.particles_50um), getType(), false});
            reading.measurements.push_back({MeasurementType::Particles100, static_cast<int32_t>(data.particles_100um), getType(), false});

            reading.measurements.push_back({MeasurementType::PM10Concentration, static_cast<float>(data.pm10_standard), getType(), false});
            reading.measurements.push_back({MeasurementType::PM25Concentration, static_cast<float>(data.pm25_standard), getType(), false});
            reading.measurements.push_back({MeasurementType::PM100Concentration, static_cast<float>(data.pm100_standard), getType(), false});

            reading.valid = true;
        } else {
            reading.valid = false;
        }
#else
        reading.measurements.push_back({MeasurementType::Particles03, static_cast<int32_t>(512), getType(), false});
        reading.measurements.push_back({MeasurementType::Particles05, static_cast<int32_t>(150), getType(), false});
        reading.measurements.push_back({MeasurementType::Particles10, static_cast<int32_t>(42), getType(), false});
        reading.measurements.push_back({MeasurementType::Particles25, static_cast<int32_t>(8), getType(), false});
        reading.measurements.push_back({MeasurementType::Particles50, static_cast<int32_t>(2), getType(), false});
        reading.measurements.push_back({MeasurementType::Particles100, static_cast<int32_t>(0), getType(), false});

        reading.measurements.push_back({MeasurementType::PM10Concentration, 5.0f, getType(), false});
        reading.measurements.push_back({MeasurementType::PM25Concentration, 8.0f, getType(), false});
        reading.measurements.push_back({MeasurementType::PM100Concentration, 10.0f, getType(), false});

        reading.valid = true;
#endif

        return reading;
    }

} // namespace Sensor
