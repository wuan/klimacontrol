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
        static const char* type() { return "ESP32"; }

        bool begin() override;
        SensorReading read() override;
        [[nodiscard]] const char* getType() const override { return type(); }
        bool isConnected() override;
        [[nodiscard]] TypeSpan provides() const override {
            static constexpr MeasurementType types[] = {
                MeasurementType::Rssi, MeasurementType::Channel,
                MeasurementType::System, MeasurementType::FreeHeap, MeasurementType::Uptime
            };
            return {types, 5};
        }
    };

} // namespace Sensor

#endif // DEVICE_SENSOR_H
