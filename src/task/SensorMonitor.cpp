#include "SensorMonitor.h"
#include "SensorController.h"
#include "control/TemperatureController.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include "Log.h"
#endif

static constexpr const char* const TAG = "sensor";

static_assert(Task::SensorMonitor::MAX_TICK_MS == SensorController::MEASUREMENT_INTERVAL_MS,
              "SensorMonitor::MAX_TICK_MS must track the default measurement interval");

namespace Task {
    
    SensorMonitor::SensorMonitor(SensorController &controller, Control::TemperatureController &control)
        : controller(controller), control(control) {
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

        // The sensor set is fixed once setup() has run (startTask() is called
        // after SensorController::begin() has registered everything found on
        // the bus), so the tick is a startup constant, not a per-cycle query.
        uint32_t tickMs = controller.minReadIntervalMs();
        if (tickMs > MAX_TICK_MS) tickMs = MAX_TICK_MS;
        if (tickMs < MIN_TICK_MS) tickMs = MIN_TICK_MS;
        ESP_LOGI(TAG, "SensorMonitor tick: %u ms", tickMs);

        unsigned long lastDiagnostics = millis();
        static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 900000; // 15 minutes

        while (true) {
            esp_task_wdt_reset();

            auto startTime = millis();
            controller.readSensors(startTime);

            // One lock take for both inputs, so temperature and validity
            // describe the same instant.
            const auto pv = controller.getProcessValue();

            // Called unconditionally. update() gates on
            // temperature_control_enabled and on `valid` itself, and it has to
            // run even while disabled so it can mark the tick as skipped — an
            // outer guard here would leave the PID thinking it was still
            // running, and the first tick after re-enabling would charge its
            // integral with the entire disabled duration.
            control.update(pv.temperature, pv.valid, startTime);

            // Periodic stack high-water mark logging for this task
            if (startTime - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
                lastDiagnostics = startTime;
                ESP_LOGI(TAG, "SensorMonitor stack HWM: %u bytes",
                         uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t));
            }

            // Sleep for the rest of the tick. `elapsed` is the time spent in
            // readSensors() and control.update(); if that overran the tick,
            // yield for one tick rather than underflow. The margin is
            // explained at WAKE_MARGIN_MS.
            const uint32_t elapsed = static_cast<uint32_t>(millis() - startTime);
            const uint32_t sleepMs = elapsed < tickMs ? tickMs - elapsed + WAKE_MARGIN_MS : 1u;

            stats.add(sleepMs);

            vTaskDelay(pdMS_TO_TICKS(sleepMs));
        }
    }
#endif
    
} // namespace Task