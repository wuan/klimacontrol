#include "network/MqttPublisher.h"

#include <cstdio>
#include <cstring>

#include "DarkModeStatusLed.h"
#include "DeviceId.h"
#include "Log.h"
#include "SensorController.h"
#include "control/TemperatureController.h"
#include "network/InternetHealth.h"
#include "ota/OTAConfig.h" // FIRMWARE_VERSION

#ifdef ARDUINO
static constexpr auto TAG = "net";
#else
#define TAG "net"
#endif

namespace Net {

    namespace {
        // Static, not stack: publishDiscovery() runs on the Network task
        // (8 KiB stack, measured peak ~3.5 KiB) and the payload can reach
        // MqttClient::MQTT_BUFFER_SIZE, so a stack copy would roughly double
        // the task's peak stack use for the duration of one connect. All MQTT
        // work happens on the Network task, so the buffer is single-threaded
        // by construction.
        char discoveryPayload[MqttClient::MQTT_BUFFER_SIZE];
    } // namespace

    void MqttPublisher::begin() {
#ifdef ARDUINO
        mqtt = std::make_unique<MqttClient>();
        wireCommandHandler();
#endif
    }

    void MqttPublisher::wireCommandHandler() {
        if (!mqtt) return;
        mqtt->setCommandHandler([this](const char* topic, const char* payload) { handleCommand(topic, payload); });
    }

    void MqttPublisher::connect(const Config::MqttConfig& config) {
        ESP_LOGI(TAG, "Initializing MQTT...");
        if (mqtt) {
            mqtt->begin(config);
        } else {
            ESP_LOGE(TAG, "MqttClient not initialized — call begin() first");
        }
        ESP_LOGI(TAG, "MQTT initialized");
    }

    void MqttPublisher::updateConfig(const Config::MqttConfig& config) const {
        if (mqtt) mqtt->setConfig(config);
    }

    void MqttPublisher::resetBackoff() {
        if (mqtt) mqtt->resetConsecutiveConnectFailures();
    }

    void MqttPublisher::tick(uint32_t nowMs, uint32_t bootMs, uint32_t epoch, InternetHealth& health) {
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

        const bool connected = mqtt->isConnected();

        // A fresh (re)connect: refresh everything a subscriber needs, before
        // (and regardless of) the measurement cadence and settle period. The
        // retained discovery message is also what makes the entities reappear
        // after a broker restart without the user re-toggling the setting.
        if (connected) {
            const uint32_t connects = mqtt->getConnectCount();
            const uint32_t computations = control.getControlComputeCount();
            if (connects != lastConnectCount) {
                lastConnectCount = connects;
                // Seed the baseline: the forced publish below already carries
                // the current state, so the same tick must not publish twice.
                lastControlComputeCount = computations;
                publishDiscovery();
                publishControlState(true);
            } else if (computations != lastControlComputeCount) {
                // A fresh control-loop computation. Republish the whole
                // control-state set so a subscriber sees the setpoint and
                // demand of each run, not only when a value changed.
                lastControlComputeCount = computations;
                publishControlState(true);
            } else {
                publishControlState(false);
            }
        } else {
            lastConnectCount = 0;
        }

        if (!connected || !sensors.isDataValid()) {
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

    void MqttPublisher::publishMeasurements(const std::vector<Sensor::Measurement>& measurements,
                                            uint32_t epoch) const {
        if (!mqtt || !mqtt->isConnected()) return;

        const char* prefix = mqtt->getPrefix();
        uint32_t succeeded = 0;
        uint32_t failed = 0;

        for (const auto& m : measurements) {
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
            ESP_LOGW(TAG, "MQTT: Published %u/%u measurements (%u failed)", succeeded, succeeded + failed, failed);
        }
    }

    void MqttPublisher::publishControlState(const bool force) {
        if (!mqtt || !mqtt->isConnected()) return;

        const char* prefix = mqtt->getPrefix();

        // Format first, then compare: what is compared is exactly what would
        // go on the wire, so a setpoint that only changes in its printed form
        // still counts as a change.
        char setpoint[sizeof(lastSetpoint)];
        formatSetpointPayload(setpoint, sizeof(setpoint), control.getTargetTemperature());
        char enabled[sizeof(lastControlEnabled)];
        formatControlEnabledPayload(enabled, sizeof(enabled), control.isControlEnabled());
        char output[sizeof(lastControlOutput)];
        formatControlOutputPayload(output, sizeof(output), control.getControlOutput());
        char heating[sizeof(lastHeating)];
        formatHeatingPayload(heating, sizeof(heating), control.isControlActive());

        // Setpoint and enable flag are retained so a subscriber that connects
        // later still learns the current value; the demand and the heating
        // state are transient and would only mislead if retained.
        struct Entry {
            const char* suffix;
            const char* payload;
            bool retain;
            char* last;
            size_t lastSize;
        };
        const Entry entries[] = {
            {"target_temperature", setpoint, true, lastSetpoint, sizeof(lastSetpoint)},
            {"control_enabled", enabled, true, lastControlEnabled, sizeof(lastControlEnabled)},
            {"control_output", output, false, lastControlOutput, sizeof(lastControlOutput)},
            {"heating", heating, false, lastHeating, sizeof(lastHeating)},
        };

        for (const auto& entry : entries) {
            if (!force && strcmp(entry.last, entry.payload) == 0) continue;

            char topic[160];
            snprintf(topic, sizeof(topic), "%s/%s", prefix, entry.suffix);

            if (mqtt->publish(topic, entry.payload, entry.retain)) {
                // Only a successful publish updates the baseline, so a topic
                // that failed is retried on the next tick rather than being
                // remembered as sent.
                strlcpy(entry.last, entry.payload, entry.lastSize);
            }
        }
    }

    void MqttPublisher::publishDiscovery() {
#ifdef ARDUINO
        if (!mqtt || !mqtt->isConnected() || !mqtt->isHaDiscoveryEnabled()) return;

        const char* deviceId = DeviceId::getDeviceId();
        // The stored name falls back to the device id on load, so this is
        // never empty; the 31-character field is the worst case the size test
        // pins.
        const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();

        char topic[128];
        formatHaDiscoveryTopic(topic, sizeof(topic), deviceId);

        // The one payload large enough to matter: sized against the full
        // requested buffer, not the possibly-degraded actual one, so a
        // degraded buffer surfaces as a failed publish below rather than as a
        // silently truncated config that Home Assistant would reject. The
        // buffer is the static discoveryPayload, not this task's stack.
        const int length = formatHaDiscoveryPayload(discoveryPayload, sizeof(discoveryPayload), deviceId,
                                                    cfg.device_name, mqtt->getPrefix(), FIRMWARE_VERSION);
        if (length < 0 || static_cast<size_t>(length) >= sizeof(discoveryPayload)) {
            ESP_LOGE(TAG, "HA discovery payload truncated: needs %d B, buffer %u B", length, mqtt->getBufferSize());
            return;
        }

        // Retained: Home Assistant must receive it even if it subscribes after
        // this connect. MqttClient::publish() logs the truncation when the
        // buffer is degraded; restate it so a missing discovery message is
        // unambiguous.
        if (!mqtt->publish(topic, discoveryPayload, true)) {
            ESP_LOGE(TAG, "HA discovery publish failed (topic=%s, %d B, buffer %u B)", topic, length,
                     mqtt->getBufferSize());
        } else {
            ESP_LOGI(TAG, "HA discovery published (%d B)", length);
        }
#endif
    }

    void MqttPublisher::handleCommand(const char* topic, const char* payload) {
        if (!mqtt) return;

        const char* prefix = mqtt->getPrefix();

        char setpointTopic[160];
        snprintf(setpointTopic, sizeof(setpointTopic), "%s/target_temperature/set", prefix);
        if (strcmp(topic, setpointTopic) == 0) {
            float setpoint = 0.0f;
            if (!parseSetpointPayload(payload, setpoint)) {
                // Reject rather than clamp, exactly as the HTTP route does: a
                // silently substituted setpoint is indistinguishable from an
                // accepted one.
                ESP_LOGW(TAG, "MQTT setpoint command rejected: '%s'", payload);
                return;
            }
            control.setTargetTemperature(setpoint);
            ESP_LOGI(TAG, "MQTT setpoint command applied: %.1f C", static_cast<double>(setpoint));
            return;
        }

        char enableTopic[160];
        snprintf(enableTopic, sizeof(enableTopic), "%s/control_enabled/set", prefix);
        if (strcmp(topic, enableTopic) == 0) {
            if (strcmp(payload, "ON") == 0) {
                control.setControlEnabled(true);
                ESP_LOGI(TAG, "MQTT control command applied: ON");
            } else if (strcmp(payload, "OFF") == 0) {
                control.setControlEnabled(false);
                ESP_LOGI(TAG, "MQTT control command applied: OFF");
            } else {
                ESP_LOGW(TAG, "MQTT control command ignored: unknown payload '%s'", payload);
            }
        }
    }

} // namespace Net
