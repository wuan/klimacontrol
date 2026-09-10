#include "network/MqttPublisher.h"

#include <cstdio>

#include "DarkModeStatusLed.h"
#include "Log.h"
#include "SensorController.h"
#include "network/InternetHealth.h"

#ifdef ARDUINO
static constexpr const char *const TAG = "net";
#else
#define TAG "net"
#endif

namespace Net {

    void MqttPublisher::begin() {
#ifdef ARDUINO
        mqtt = std::make_unique<MqttClient>();
#endif
    }

    void MqttPublisher::connect(const Config::MqttConfig &config) {
        ESP_LOGI(TAG, "Initializing MQTT...");
        if (mqtt) {
            mqtt->begin(config);
        } else {
            ESP_LOGE(TAG, "MqttClient not initialized — call begin() first");
        }
        ESP_LOGI(TAG, "MQTT initialized");
    }

    void MqttPublisher::updateConfig(const Config::MqttConfig &config) {
        if (mqtt) mqtt->setConfig(config);
    }

    void MqttPublisher::resetBackoff() {
        if (mqtt) mqtt->resetConsecutiveConnectFailures();
    }

    void MqttPublisher::tick(uint32_t nowMs, uint32_t bootMs, uint32_t epoch, InternetHealth &health) {
        if (!mqtt) return;

        // Bridge connect failures into the internet-health counter; a recovery
        // clears it.
        const uint32_t failures = mqtt->getConsecutiveConnectFailures();
        if (failures > lastReportedFailures) {
            for (uint32_t i = lastReportedFailures; i < failures; i++) {
                health.reportFailure();
            }
            lastReportedFailures = failures;
        } else if (lastReportedFailures > 0 && failures == 0) {
            health.reportSuccess();
            lastReportedFailures = 0;
        }

        mqtt->loop();

        if (!mqtt->isConnected() || !sensors.isDataValid()) {
            statusLed.setProgress(0.0f);
            return;
        }

        const uint32_t intervalMs = mqtt->getIntervalMs();
        if (lastPublishMs == 0) lastPublishMs = nowMs; // seed on the first eligible cycle

        const bool settled = nowMs - bootMs >= SETTLE_MS;
        if (intervalMs > 0 && settled && nowMs - lastPublishMs >= intervalMs) {
            // Atomic snapshot: validity and data are read under the same lock,
            // so we never publish stale measurements after a fresh read failed.
            const auto measurements = sensors.getValidMeasurements();
            if (!measurements.empty()) {
                statusLed.setState(LedState::TRANSMIT_DATA);
                lastPublishMs = nowMs;
                publishMeasurements(measurements, epoch);
            }
        }

        if (intervalMs > 0 && settled) {
            float progress = static_cast<float>(nowMs - lastPublishMs) / intervalMs;
            if (progress > 1.0f) progress = 1.0f;
            statusLed.setProgress(progress);
            statusLed.setState(LedState::ON);
        }
    }

    void MqttPublisher::publishMeasurements(const std::vector<Sensor::Measurement> &measurements,
                                            uint32_t epoch) {
        if (!mqtt || !mqtt->isConnected()) return;

        const char *prefix = mqtt->getPrefix();
        uint32_t succeeded = 0;
        uint32_t failed = 0;

        for (const auto &m: measurements) {
            char topic[128];
            snprintf(topic, sizeof(topic), "%s/%s", prefix, Sensor::measurementTypeLabel(m.type));

            char payload[256];
            formatMeasurementPayload(payload, sizeof(payload), m, epoch);

            if (mqtt->publish(topic, payload)) {
                succeeded++;
            } else {
                failed++;
            }
        }

        mqtt->recordPublishResult(succeeded, failed);

        if (failed > 0) {
            ESP_LOGW(TAG, "MQTT: Published %u/%u measurements (%u failed)",
                     succeeded, succeeded + failed, failed);
        }
    }

} // namespace Net
