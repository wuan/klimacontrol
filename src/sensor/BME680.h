#ifndef BME680_H
#define BME680_H

#include "I2CSensor.h"

#ifdef ARDUINO
#include <Adafruit_BME680.h>
#endif

namespace Sensor {

    class BME680 : public I2CSensor {
#ifdef ARDUINO
        Adafruit_BME680 bme;
#endif

    public:
        explicit BME680(uint8_t address = 0x77);

        bool begin() override;
        SensorReading read() override;
        const char* getName() const override;
        const char* getType() const override;
    };

} // namespace Sensor

#endif // BME680_H
