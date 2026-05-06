#ifndef DEVICE_SENSOR_H
#define DEVICE_SENSOR_H

#include "Sensor.h"

namespace Sensor {

    /**
     * Virtual sensor that reports ESP32 device metrics:
     * WiFi RSSI, chip temperature, free heap, uptime
     */
    class DeviceSensor : public Sensor {
    public:
        DeviceSensor() = default;
        static const char* type() { return "ESP32"; }

        bool begin() override;
        SensorReading read(const ReadConfig& config, const std::vector<Measurement>& prior) override;
        [[nodiscard]] const char* getType() const override { return type(); }
        [[nodiscard]] TypeSpan providesMeasurements() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Rssi, MeasurementType::Channel,
                MeasurementType::System, MeasurementType::FreeHeap, MeasurementType::Uptime
            };
            return {types, 5};
        }
    };

} // namespace Sensor

#endif // DEVICE_SENSOR_H
