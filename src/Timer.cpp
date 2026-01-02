#include "Timer.h"

#ifdef ARDUINO
#include <esp32-hal.h>
#else
// Stub millis() for native tests
#include <chrono>

unsigned long millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}
#endif

namespace Support {
    Timer::Timer() : start_time(millis()) {
        lap_start_time = start_time;
    }

    unsigned long Timer::lap() {
        auto lap_time = millis();
        auto result = lap_time - lap_start_time;
        lap_start_time = lap_time;
        return result;
    }

    unsigned long Timer::elapsed() {
        return millis() - start_time;
    }
} // Support