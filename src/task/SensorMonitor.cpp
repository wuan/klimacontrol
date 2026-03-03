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
        xTaskCreate(
            taskWrapper, // Task Function
            "SensorMonitor", // Task Name
            12000, // Stack Size (12KB)
            this, // Parameters
            1, // Priority (same as LED task)
            &taskHandle // Task Handle
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
        while (true) {
            auto startTime = millis();
            controller.readSensors();

            if (controller.isControlEnabled()) {
                controller.updateControl();
            }
            unsigned long elapsed = millis() - startTime;
            unsigned long duration = elapsed < readingInterval ? readingInterval - elapsed : 1ul;

            vTaskDelay(duration / portTICK_PERIOD_MS);
        }
    }
#endif
    
} // namespace Task