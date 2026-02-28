#ifndef I2CSENSOR_H
#define I2CSENSOR_H

#include "Sensor.h"

#ifdef ARDUINO
#include <Wire.h>
#endif

namespace Sensor {

    class I2CSensor : public Sensor {
    protected:
        bool initialized;
        uint8_t i2cAddress;
#ifdef ARDUINO
        TwoWire &wire;
#endif

    public:
#ifdef ARDUINO
        explicit I2CSensor(uint8_t address = 0x77, TwoWire &wire = Wire1);
#else
        explicit I2CSensor(uint8_t address = 0x77);
#endif
        bool isConnected() override;
    };

} // namespace Sensor

#endif // I2CSensor_H
