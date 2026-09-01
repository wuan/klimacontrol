#include "display/DisplayManager.h"
#include "display/EPaperDisplay.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <variant>

#include "Log.h"
#include "Network.h"
#include "SensorController.h"
#include "support/LocalTime.h"

static const char *TAG = "display";

namespace Display {

    namespace {
        // Pull a float measurement out of an already-captured snapshot. Using
        // the snapshot rather than SensorController's individual accessors
        // keeps temperature, humidity and the validity flag describing the same
        // instant — each accessor takes the lock separately.
        float floatFrom(const std::vector<Sensor::Measurement> &measurements,
                        Sensor::MeasurementType type) {
            const auto *m = Sensor::findMeasurement(measurements, type);
            if (m == nullptr) {
                return NAN;
            }
            const float *f = std::get_if<float>(&m->value);
            return f != nullptr ? *f : NAN;
        }
    } // namespace

    DisplayManager::DisplayManager(SensorController &controller)
        : controller(controller),
          policy(Config::DEFAULT_DISPLAY_INTERVAL) {
        // Created here rather than lazily: this object is a file-scope
        // singleton, and on this core FreeRTOS is already running by the time
        // static constructors execute (SensorController does the same).
        panelMutex = xSemaphoreCreateMutex();
    }

    bool DisplayManager::begin(const Config::DisplayConfig &config, const char *deviceNameIn) {
        strlcpy(deviceName, deviceNameIn != nullptr ? deviceNameIn : "", sizeof(deviceName));

        policy = RefreshPolicy(config.interval);

        if (!panel.begin(config.rotation)) {
            ESP_LOGE(TAG, "Display initialisation faulted");
            enabled = false;
            return false;
        }

        panel.showSplash(deviceName);
        enabled = true;
        ESP_LOGI(TAG, "Display enabled (rotation=%u, min interval=%u s)",
                 config.rotation, config.interval);
        return true;
    }

    void DisplayManager::disableAndClear() {
        // Stop the Network task repainting first, so that once we hold the lock
        // there is no further work queued behind us.
        enabled = false;

        // Wait for any refresh already in flight. portMAX_DELAY is safe: the
        // holder is a bounded panel operation, itself capped by EPaperDisplay's
        // fault guard.
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "Could not acquire panel lock to clear the display");
            return;
        }

        // e-paper retains its image with no power, so a display that has been
        // turned off has to be actively blanked or it keeps showing a stale
        // reading forever.
        panel.clear();
        ESP_LOGI(TAG, "Display disabled and blanked");

        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

    void DisplayManager::formatClock(char *out, size_t n) const {
        if (out == nullptr || n == 0) {
            return;
        }
        out[0] = '\0';
        if (network == nullptr) {
            return;
        }
        // Local time, daylight saving included. formatLocalHhMm() leaves the
        // buffer empty for epoch 0 (NTP not synced), so the footer field stays
        // blank rather than claiming a time.
        Support::formatLocalHhMm(out, n, network->getCurrentEpoch());
    }

    void DisplayManager::update() {
        // Cheap pre-checks before touching the lock at all.
        if (!enabled || panel.hasFaulted()) {
            return;
        }

        // Held for the whole body. An uncontended take is on the order of a
        // microsecond, and holding it throughout means `enabled` cannot flip
        // under us between the policy decision and the repaint — otherwise a
        // concurrent disableAndClear() could blank the panel and we would
        // immediately paint over it.
        //
        // Short timeout, not a block: if the web handler holds the lock we are
        // being disabled anyway, and any genuine change repaints on a later
        // tick because the policy only commits when a refresh actually happens.
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return;
        }

        if (enabled) {
            const SensorController::Snapshot snapshot = controller.getSnapshot();
            const float temperature = floatFrom(snapshot.measurements, Sensor::MeasurementType::Temperature);
            const float humidity = floatFrom(snapshot.measurements, Sensor::MeasurementType::RelativeHumidity);

            // Wall-clock minute drives the footer, so the panel keeps a live
            // clock. epoch/60 ticks at the same instant in every timezone (all
            // real offsets are whole minutes), so this needs no TZ awareness.
            // 0 while NTP is unsynced, matching getCurrentEpoch()'s sentinel.
            const uint32_t epoch = network != nullptr ? network->getCurrentEpoch() : 0;
            const uint32_t clockMinute = epoch / 60u;

            const RefreshKind kind =
                policy.evaluate(temperature, humidity, snapshot.valid, millis(), clockMinute);
            if (kind != RefreshKind::None) {
                const bool available =
                    snapshot.valid && !std::isnan(temperature) && !std::isnan(humidity);

                char tempStr[16];
                char humStr[16];
                formatTemperature(tempStr, sizeof(tempStr), temperature, available);
                formatHumidity(humStr, sizeof(humStr), humidity, available);

                char clock[8];
                formatClock(clock, sizeof(clock));

                // Determine control state
                Display::ControlState controlState;
                if (!controller.isControlEnabled()) {
                    controlState = Display::ControlState::INACTIVE;
                } else if (controller.isControlActive()) {
                    controlState = Display::ControlState::ACTIVE_ON;
                } else {
                    controlState = Display::ControlState::ACTIVE_OFF;
                }

                // Format setpoint
                char setpointStr[8];
                float target = controller.getTargetTemperature();
                if (std::isnan(target)) {
                    snprintf(setpointStr, sizeof(setpointStr), "--");
                } else {
                    snprintf(setpointStr, sizeof(setpointStr), "%.1f", static_cast<double>(target));
                }

                // Bare numbers: EPaperDisplay owns the unit decoration, because
                // the degree mark has to be drawn geometrically (the GFX fonts
                // only carry glyphs 0x20-0x7E) rather than printed.
                panel.render(tempStr, humStr, deviceName, clock, controlState, setpointStr, kind);
            }
        }

        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

} // namespace Display

#endif // ARDUINO
