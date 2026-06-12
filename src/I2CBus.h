#ifndef I2C_BUS_H
#define I2C_BUS_H

// Shared lock serializing access to the I2C bus (Wire1) between the
// SensorMonitor task (periodic sensor reads) and the web server task
// (the /api/i2c/scan endpoint).
//
// The Arduino TwoWire driver only locks individual transactions. A logical
// sensor read is several transactions (e.g. write command, then read result),
// so without a higher-level lock a concurrent bus scan can interleave between
// them and corrupt the reading. This guard makes each side's bus sequence
// atomic with respect to the other.

#ifdef ARDUINO

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace I2CBus {

    // Lazily-created shared mutex. C++ guarantees thread-safe initialization of
    // the function-local static, and the heap is available well before either
    // caller runs.
    inline SemaphoreHandle_t mutex() {
        static SemaphoreHandle_t m = xSemaphoreCreateMutex();
        return m;
    }

    // RAII guard. After construction, check the bool conversion: `held` is false
    // if the mutex could not be acquired within the timeout.
    struct Lock {
        SemaphoreHandle_t handle;
        bool held;

        explicit Lock(TickType_t timeout = portMAX_DELAY)
            : handle(mutex()), held(handle && xSemaphoreTake(handle, timeout) == pdTRUE) {}

        ~Lock() {
            if (held) xSemaphoreGive(handle);
        }

        Lock(const Lock &) = delete;
        Lock &operator=(const Lock &) = delete;

        explicit operator bool() const { return held; }
    };

} // namespace I2CBus

#endif // ARDUINO

#endif // I2C_BUS_H
