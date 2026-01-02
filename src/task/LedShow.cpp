#include "LedShow.h"

#include "Timer.h"

namespace Task {
    void LedShow::startTask() {
#ifdef ARDUINO
        xTaskCreatePinnedToCore(
            taskWrapper, // Task Function
            "LED show", // Task Name
            10000, // Stack Size
            this, // Parameters
            1, // Priority
            &taskHandle, // Task Handle
            1 // Core Number
        );
#endif
    }

#ifdef ARDUINO
    void LedShow::taskWrapper(void *pvParameters) {
        Serial.println("LedShow: taskWrapper()");
        auto *instance = static_cast<LedShow *>(pvParameters);
        instance->task();
    }

    void LedShow::task() {
        unsigned int iteration = 0;
        unsigned long total_execution_time = 0;
        unsigned long total_show_time = 0;

        unsigned long start_time = millis();
        unsigned long last_show_stats = millis();
        unsigned long show_cycle_time = controller.getCycleTime();

        // Power save mode - reduce update frequency when display is static
        const unsigned long power_save_cycle_time = 250; // 250ms when static (4 Hz)
        bool in_power_save = false;

        while (true) {
            // Process any pending show change commands from webserver
            controller.processCommands();

            auto timer = Support::Timer();

            controller.executeShow(iteration++);
            auto execution_time = timer.lap();

            controller.show();
            auto show_time = timer.lap();

            total_execution_time += execution_time;
            total_show_time += show_time;

            // Check if show is static for power save mode
            bool show_is_complete = controller.isShowComplete();
            unsigned long effective_cycle_time = show_is_complete ? power_save_cycle_time : show_cycle_time;
            auto delay = effective_cycle_time - std::min(effective_cycle_time, timer.elapsed());

            // Log power save state transitions
            if (show_is_complete && !in_power_save) {
                Serial.println("LedShow: Entering power save mode (static display)");
                in_power_save = true;
            } else if (!show_is_complete && in_power_save) {
                Serial.println("LedShow: Exiting power save mode");
                in_power_save = false;
            }

            // Update stats in controller every second (roughly)
            if (iteration % (1000 / std::max(1u, (unsigned int)effective_cycle_time)) == 0) {
                ShowStats stats;
                stats.last_execution_time = execution_time;
                stats.last_show_time = show_time;
                stats.avg_execution_time = total_execution_time / iteration;
                stats.avg_show_time = total_show_time / iteration;
                stats.avg_cycle_time = (timer.start_time - start_time) / iteration;
                controller.updateStats(stats);
            }

            // Log stats every 60 seconds to reduce Serial blocking
            if (timer.start_time - last_show_stats > 60000) {
                Serial.printf(
                    "Durations: execution %lu ms (avg: %lu ms), show %lu ms (avg: %lu ms), avg. cycle %lu ms, delay %lu ms%s\n",
                    execution_time, total_execution_time / iteration,
                    show_time, total_show_time / iteration,
                    (timer.start_time - start_time) / iteration, delay,
                    in_power_save ? " [POWER SAVE]" : "");
                last_show_stats = timer.start_time;
            }
            vTaskDelay(delay / portTICK_PERIOD_MS);
        }
    }

    LedShow::LedShow(ShowController &controller) : controller(controller) {
    }
#endif
}
