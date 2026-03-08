#ifndef SENSOR_MONITOR_H
#define SENSOR_MONITOR_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "support/Stats.h"

class SensorController;

namespace Task {
    
    /**
     * Sensor Monitoring Task
     * Runs on Core 1, reads sensors and updates temperature control
     */
    class SensorMonitor {
    private:
        SensorController &controller;
        TaskHandle_t taskHandle = nullptr;
        Support::Stats stats;
        
        unsigned long readingInterval = 1000;
        
    public:
        /**
         * Constructor
         * @param controller Sensor controller reference
         */
        explicit SensorMonitor(SensorController &controller);
        
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