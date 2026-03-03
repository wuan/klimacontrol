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
        unsigned long lastDiagnostics = millis();
        static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 300000; // 5 minutes

        while (true) {
            auto startTime = millis();
            controller.readSensors();

            if (controller.isControlEnabled()) {
                controller.updateControl();
            }

            // Periodic stack high-water mark logging for this task
            if (startTime - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
                lastDiagnostics = startTime;
                Serial.printf("SensorMonitor stack HWM: %u bytes\r\n",
                              uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t));
            }

            unsigned long elapsed = millis() - startTime;
            unsigned long duration = elapsed < readingInterval ? readingInterval - elapsed : 1ul;

            vTaskDelay(duration / portTICK_PERIOD_MS);
        }
    }
#endif
    
} // namespace Task