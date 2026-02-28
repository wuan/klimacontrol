#ifndef I2CSENSOR_H
#define I2CSENSOR_H

#include "Sensor.h"

namespace Sensor {

    class I2CSensor : public Sensor {
    protected:
        bool initialized;
        uint8_t i2cAddress;

    public:
        explicit I2CSensor(uint8_t address = 0x77);
        bool isConnected() override;
    };

} // namespace Sensor

#endif // I2CSensor_H
