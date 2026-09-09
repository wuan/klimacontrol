#ifndef SENSOR_MONITOR_H
#define SENSOR_MONITOR_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "support/Stats.h"

class SensorController;

namespace Control {
    class TemperatureController;
}

namespace Task {
    
    /**
     * Sensor Monitoring Task
     * Reads sensors and drives the temperature control loop with the result.
     * Ticks at the shortest interval any configured sensor needs
     * (SensorController::minReadIntervalMs), fixed at task start: 1 s with
     * an SGP40 fitted, 15 s otherwise.
     */
    class SensorMonitor {
    private:
        SensorController &controller;
        Control::TemperatureController &control;
        TaskHandle_t taskHandle = nullptr;
        Support::Stats stats;

    public:
        /**
         * Tick bounds. The tick is chosen once, at task start, from
         * SensorController::minReadIntervalMs(). MAX_TICK_MS keeps the task
         * ticking (init retries, control-loop skipped-tick bookkeeping, TWDT
         * feed) with 2x margin under the 30 s task watchdog even when nothing
         * is fitted; it must equal MEASUREMENT_INTERVAL_MS, asserted in the .cpp.
         */
        static constexpr uint32_t MIN_TICK_MS = 100;
        static constexpr uint32_t MAX_TICK_MS = 15000;

        /**
         * Added to every sleep. vTaskDelay(n) returns at a tick boundary, so
         * the real wait is anywhere in (n-1, n] ms and millis() can come up
         * 1 ms short of the interval. At a 1 s tick that slipped a default
         * read by one harmless second; at a 15 s tick it would skip a whole
         * cycle and leave a 30 s gap. Two ticks of margin makes the wake
         * strictly late, and the default phase rebases to `now` anyway.
         */
        static constexpr uint32_t WAKE_MARGIN_MS = 2;

        /**
         * Constructor
         * @param controller Sensor controller reference
         * @param control    Control loop, fed from the controller each tick
         */
        SensorMonitor(SensorController &controller, Control::TemperatureController &control);
        
        /**
         * Start the sensor monitoring task
         */
        void startTask();
        
        /**
         * Get task handle
         * @return Task handle
         */
        TaskHandle_t getTaskHandle() const { return taskHandle; }

        /**
         * Get cycle delay stats
         */
        const Support::Stats& getStats() const { return stats; }

        /**
         * Take an indivisible copy of the cycle-delay counters. Cross-task
         * readers (e.g. GET /api/about on the AsyncTCP task) SHALL go
         * through this accessor rather than the per-field getters —
         * see the "Cross-task reads of `Support::Stats` use a snapshot
         * accessor" requirement in
         * openspec/specs/system-architecture/spec.md.
         */
        Support::StatsSnapshot getStatsSnapshot() const { return stats.snapshot(); }
        
    private:
        /**
         * Main sensor monitoring task
         */
        [[noreturn]] void task();
        
        /**
         * Static trampoline function for FreeRTOS
         */
        static void taskWrapper(void *pvParameters);
    };
    
} // namespace Task

#endif // SENSOR_MONITOR_H