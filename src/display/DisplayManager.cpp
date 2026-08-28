#include "display/DisplayManager.h"

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

    void DisplayManager::clearAndPark(const Config::DisplayConfig &config) {
        // e-paper retains its image with no power, so a display that has been
        // turned off has to be actively blanked or it keeps showing a stale
        // reading forever.
        if (!panel.begin(config.rotation)) {
            ESP_LOGW(TAG, "Could not initialise display to clear it");
            return;
        }
        panel.clear();
        enabled = false;
        ESP_LOGI(TAG, "Display disabled and blanked");
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
        if (!enabled || panel.hasFaulted()) {
            return;
        }

        const SensorController::Snapshot snapshot = controller.getSnapshot();
        const float temperature = floatFrom(snapshot.measurements, Sensor::MeasurementType::Temperature);
        const float humidity = floatFrom(snapshot.measurements, Sensor::MeasurementType::RelativeHumidity);

        const RefreshKind kind = policy.evaluate(temperature, humidity, snapshot.valid, millis());
        if (kind == RefreshKind::None) {
            return;
        }

        const bool available = snapshot.valid && !std::isnan(temperature) && !std::isnan(humidity);

        char tempStr[16];
        char humStr[16];
        formatTemperature(tempStr, sizeof(tempStr), temperature, available);
        formatHumidity(humStr, sizeof(humStr), humidity, available);

        char clock[8];
        formatClock(clock, sizeof(clock));

        // Bare numbers: EPaperDisplay owns the unit decoration, because the
        // degree mark has to be drawn geometrically (the GFX fonts only carry
        // glyphs 0x20-0x7E) rather than printed as a character.
        panel.render(tempStr, humStr, deviceName, clock, kind);
    }

} // namespace Display

#endif // ARDUINO
