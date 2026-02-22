#include "SensorMonitor.h"
#include "SensorController.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace Task {
    
    SensorMonitor::SensorMonitor(SensorController &controller)
        : controller(controller) {
    }
    
    void SensorMonitor::startTask() {
#ifdef ARDUINO
        xTaskCreatePinnedToCore(
            taskWrapper, // Task Function
            "SensorMonitor", // Task Name
            8000, // Stack Size (8KB)
            this, // Parameters
            1, // Priority (same as LED task)
            &taskHandle, // Task Handle
            1 // Core Number (same as LED task)
        );
#endif
    }
    
#ifdef ARDUINO
    void SensorMonitor::taskWrapper(void *pvParameters) {
        Serial.println("SensorMonitor: taskWrapper()");
        auto *instance = static_cast<SensorMonitor *>(pvParameters);
        instance->task();
    }
    
    void SensorMonitor::task() {
        unsigned long lastStatsTime = millis();
        unsigned long lastControlUpdate = millis();
        
        while (true) {
            unsigned long currentTime = millis();
            
            // Read sensors at the configured interval
            if (currentTime - lastReadingTime >= readingInterval) {
                controller.readSensors();
                lastReadingTime = currentTime;
                
                // Log sensor readings
                const auto &data = controller.getCurrentData();
                if (data.valid) {
                    Serial.printf("Sensors: T=%.1f°C, H=%.1f%%, age=%lu ms\n",
                                 data.temperature, data.humidity,
                                 controller.getTimeSinceLastReading());
                } else {
                    Serial.println("Sensors: No valid data");
                }
            }
            
            // Update temperature control every second
            if (currentTime - lastControlUpdate >= 1000) {
                if (controller.isControlEnabled()) {
                    float controlOutput = controller.updateControl();
                    // TODO: Apply control output to actuator (relay, etc.)
                }
                lastControlUpdate = currentTime;
            }
            
            // Log statistics every 60 seconds
            if (currentTime - lastStatsTime >= 60000) {
                Serial.printf("SensorMonitor: Running for %lu ms, interval=%lu ms\n",
                             currentTime, readingInterval);
                lastStatsTime = currentTime;
            }
            
            // Small delay to prevent task from hogging CPU
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
#endif
    
} // namespace Task