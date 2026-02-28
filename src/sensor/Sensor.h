#ifndef SENSOR_H
#define SENSOR_H

#include <cmath>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace Sensor {

    using Value = std::variant<float, int32_t>;

    struct Measurement {
        const char* type;       // e.g., "temperature", "humidity", "pressure", "co2"
        Value value;
        const char* unit;       // e.g., "°C", "%", "hPa", "ppm"
        const char* sensor;     // e.g., "SHT4x", "BME680"
        bool calculated;        // true for derived values (dew point, sea-level pressure)
    };

    // Magnus formula dew point from temperature (°C) and relative humidity (%)
    inline float calcDewPoint(float temperature, float humidity) {
        constexpr float a = 17.625f;
        constexpr float b = 243.04f;
        float gamma = a * temperature / (b + temperature) + logf(humidity / 100.0f);
        return b * gamma / (a - gamma);
    }

    struct SensorReading {
        std::vector<Measurement> measurements;
        uint32_t timestamp;
        bool valid;
        SensorReading() : timestamp(0), valid(false) {}
    };

    /**
     * Base sensor interface
     */
    class Sensor {
    public:
        virtual ~Sensor() = default;
        virtual bool begin() = 0;
        virtual SensorReading read() = 0;
        virtual const char* getType() const = 0;
        virtual bool isConnected() = 0;
    };

    /**
     * Sensor factory interface for creating sensor instances
     */
    class SensorFactory {
    public:
        virtual ~SensorFactory() = default;
        virtual std::unique_ptr<Sensor> createSensor() = 0;
    };

} // namespace Sensor

#endif // SENSOR_H
