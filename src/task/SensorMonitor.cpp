#include "SensorMonitor.h"
#include "SensorController.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
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
            16000, // Stack Size (16KB)
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
        esp_task_wdt_add(NULL);

        unsigned long lastDiagnostics = millis();
        static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 300000; // 5 minutes

        while (true) {
            esp_task_wdt_reset();

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

            stats.add(duration);

            vTaskDelay(duration / portTICK_PERIOD_MS);
        }
    }
#endif
    
} // namespace Task