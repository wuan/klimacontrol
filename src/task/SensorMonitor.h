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
     * Reads sensors and drives the temperature control loop with the result
     */
    class SensorMonitor {
    private:
        SensorController &controller;
        Control::TemperatureController &control;
        TaskHandle_t taskHandle = nullptr;
        Support::Stats stats;
        
        unsigned long readingInterval = 1000;
        
    public:
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
         * Set reading interval
         * @param intervalMs Interval in milliseconds
         */
        void setReadingInterval(unsigned long intervalMs) { 
            readingInterval = intervalMs; 
        }
        
        /**
         * Get current reading interval
         * @return Current interval in milliseconds
         */
        unsigned long getReadingInterval() const { return readingInterval; }
        
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