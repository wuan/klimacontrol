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

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace I2CBus {

    // Attempt to recover a wedged I2C bus. If a slave was reset (or glitched) mid
    // transaction it can be left holding SDA low, which jams every subsequent
    // transfer for the whole bus — the classic symptom is every sensor read
    // failing at once with no way out but a power cycle.
    //
    // Recovery: release the Wire1 peripheral, then bit-bang up to 9 clock pulses
    // on SCL so the stuck slave can finish shifting out its byte and let go of
    // SDA, emit a STOP condition, and hand the pins back to the I2C driver.
    //
    // Must be called with the bus Lock held (see below). Returns true if SDA is
    // released (HIGH) afterwards, i.e. the bus looks usable again. Relies on the
    // external bus pull-ups present on the STEMMA QT connector.
    inline bool recover() {
        const int sda = SDA1;
        const int scl = SCL1;

        Wire1.end(); // release the peripheral so we can drive the pins manually

        pinMode(scl, OUTPUT_OPEN_DRAIN);
        pinMode(sda, INPUT_PULLUP);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);

        bool released = false;
        for (int i = 0; i < 9; ++i) {
            if (digitalRead(sda) == HIGH) {
                released = true;
                break;
            }
            digitalWrite(scl, LOW);
            delayMicroseconds(5);
            digitalWrite(scl, HIGH);
            delayMicroseconds(5);
        }
        released = released || (digitalRead(sda) == HIGH);

        // STOP condition: SDA low->high while SCL is high.
        pinMode(sda, OUTPUT_OPEN_DRAIN);
        digitalWrite(sda, LOW);
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
        digitalWrite(sda, HIGH);
        delayMicroseconds(5);

        Wire1.begin(sda, scl); // hand the pins back to the I2C peripheral
        return released;
    }

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
