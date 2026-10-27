#ifndef KLIMACONTROL_NET_MQTT_PUBLISHER_H
#define KLIMACONTROL_NET_MQTT_PUBLISHER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Config.h"
#include "MqttClient.h"
#include "network/MqttPayload.h"
#include "sensor/Sensor.h"

class DarkModeStatusLed;
class SensorController;

namespace Config {
    class ConfigManager;
}

namespace Control {
    class TemperatureController;
}

namespace Net {

    class InternetHealth;

    /**
     * Drives the long-lived `MqttClient` from the Network task: keeps the
     * connection alive, publishes the sensor snapshot on the configured
     * interval, publishes the control/actuator state on change, mirrors
     * publish progress onto the status LED and bridges connect failures into
     * `InternetHealth`.
     *
     * The client is constructed once in `begin()` and re-initialised in place
     * on every (re)connect; see spec `memory-management` → "Long-lived
     * singletons are constructed once".
     */
    class MqttPublisher {
    public:
        /** Suppress publishing for this long after boot so sensors settle. */
        static constexpr uint32_t SETTLE_MS = 60000;

        /**
         * @param sensors   Sensor cache (measurement snapshots).
         * @param statusLed Publish-progress indicator.
         * @param config    Device configuration (device name for discovery).
         * @param control   Control loop. Read-only here: this publishes the
         *                  setpoint, enable flag, demand and confirmed heating,
         *                  and applies inbound commands through its setter
         *                  methods. Every member it touches is either a plain
         *                  scalar written by the Sensor Monitor task or a
         *                  config write guarded by ConfigManager, so no new
         *                  cross-task writer is introduced — this object only
         *                  reads the loop from the Network task that already
         *                  owns it.
         */
        MqttPublisher(SensorController& sensors, DarkModeStatusLed& statusLed, Config::ConfigManager& config,
                      Control::TemperatureController& control)
            : sensors(sensors), statusLed(statusLed), config(config), control(control) {}

        MqttPublisher(const MqttPublisher&) = delete;
        MqttPublisher& operator=(const MqttPublisher&) = delete;

        /** Construct the client. Call once from setup(), before the task runs. */
        void begin();

        /** (Re)initialise the client with `config`; idempotent, no re-allocation. */
        void connect(const Config::MqttConfig& config);

        /** Apply a new broker configuration at runtime. */
        void updateConfig(const Config::MqttConfig& config) const;

        /**
         * Once-per-second work: failure bridging, keepalive/reconnect, and a
         * publish when the interval has elapsed. `epoch` is the NTP time to
         * stamp on the payloads (0 when unsynced).
         */
        void tick(uint32_t nowMs, uint32_t bootMs, uint32_t epoch, InternetHealth& health);

        void publishMeasurements(const std::vector<Sensor::Measurement>& measurements, uint32_t epoch) const;

        /** WiFi came back: restart the publish timer so there is no burst. */
        void onWifiReconnected(uint32_t nowMs) { lastPublishMs = nowMs; }

        /**
         * Drop back to the base reconnect backoff so MQTT returns within
         * seconds instead of the minutes suspended attempts escalated to.
         */
        void resetBackoff();

        /** nullptr until begin() has run. */
        MqttClient* client() { return mqtt.get(); }

    private:
        /**
         * Publish the control/actuator state. `force` republishes every topic
         * (used once per connect); otherwise only the topics whose payload
         * changed since the last publish are sent. Not gated by the
         * measurement interval or the post-boot settle period: a setpoint
         * change should reach a subscriber within a tick of the loop.
         */
        void publishControlState(bool force);

        /** Publish the retained Home Assistant discovery message, if enabled. */
        void publishDiscovery();

        /**
         * Apply an inbound MQTT command. Runs on the Network task, from inside
         * the PubSubClient callback that `MqttClient::loop()` pumps.
         */
        void handleCommand(const char* topic, const char* payload);

        /** Register the command sink with the client. Call once, after begin(). */
        void wireCommandHandler();

        SensorController& sensors;
        DarkModeStatusLed& statusLed;
        Config::ConfigManager& config;
        Control::TemperatureController& control;
        std::unique_ptr<MqttClient> mqtt;
        uint32_t lastPublishMs = 0;
        uint32_t lastReportedFailures = 0;

        // Fresh-connect detection for the event-driven publishes. MqttClient
        // counts successful connects so a config change that drops and
        // re-establishes the link is seen as a new connect, not as "unchanged".
        uint32_t lastConnectCount = 0;

        // Last published control-state payloads, compared against to publish
        // only on change. Empty until the first (forced) publish.
        char lastSetpoint[16] = "";
        char lastControlEnabled[8] = "";
        char lastControlOutput[8] = "";
        char lastHeating[8] = "";
    };

} // namespace Net

#endif // KLIMACONTROL_NET_MQTT_PUBLISHER_H
