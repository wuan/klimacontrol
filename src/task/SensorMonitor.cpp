#include "SensorMonitor.h"
#include "SensorController.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "Log.h"
#endif

static const char* TAG = "sensor";

namespace Task {
    
    SensorMonitor::SensorMonitor(SensorController &controller)
        : controller(controller) {
    }
    
    void SensorMonitor::startTask() {
#ifdef ARDUINO
        // Stack size is measured, not guessed. This stack comes out of *internal*
        // SRAM (xTaskCreate can never place it in PSRAM), which is the pool OTA
        // downloads, lwIP and AsyncTCP compete for — at 16000 B it was the single
        // largest consumer after the Network task while using 12.9% of it.
        // Measured peak after a full read cycle over all I2C sensors: 2056 B used
        // (HWM reported 13944 B free of 16000). 6144 gives ~3x headroom; the
        // periodic "SensorMonitor stack HWM" line below re-measures it, so raise
        // this if that number ever approaches 0.
        xTaskCreate(
            taskWrapper, // Task Function
            "SensorMonitor", // Task Name
            6144, // Stack Size (measured peak 2056 B, ~3x headroom)
            this, // Parameters
            1, // Priority (same as Network task)
            &taskHandle // Task Handle
        );
#endif
    }
    
#ifdef ARDUINO
    void SensorMonitor::taskWrapper(void *pvParameters) {
        ESP_LOGI(TAG, "SensorMonitor: taskWrapper()");
        auto *instance = static_cast<SensorMonitor *>(pvParameters);
        instance->task();
    }
    
    void SensorMonitor::task() {
        // Subscribe to the TWDT. setup() initializes the TWDT before creating
        // this task, so this should always succeed; log loudly if it does not,
        // because an unsubscribed task makes every esp_task_wdt_reset() below a
        // silent no-op and removes the 30s stall protection entirely.
        esp_err_t wdtAdd = esp_task_wdt_add(NULL);
        if (wdtAdd != ESP_OK) {
            ESP_LOGE(TAG, "esp_task_wdt_add failed (err 0x%x) - task runs unguarded", wdtAdd);
        }

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
                ESP_LOGI(TAG, "SensorMonitor stack HWM: %u bytes",
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