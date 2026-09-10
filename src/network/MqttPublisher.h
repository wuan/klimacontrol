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

namespace Net {

    class InternetHealth;

    /**
     * Drives the long-lived `MqttClient` from the Network task: keeps the
     * connection alive, publishes the sensor snapshot on the configured
     * interval, mirrors publish progress onto the status LED and bridges
     * connect failures into `InternetHealth`.
     *
     * The client is constructed once in `begin()` and re-initialised in place
     * on every (re)connect; see spec `memory-management` → "Long-lived
     * singletons are constructed once".
     */
    class MqttPublisher {
    public:
        /** Suppress publishing for this long after boot so sensors settle. */
        static constexpr uint32_t SETTLE_MS = 60000;

        MqttPublisher(SensorController &sensors, DarkModeStatusLed &statusLed)
            : sensors(sensors), statusLed(statusLed) {}

        MqttPublisher(const MqttPublisher &) = delete;
        MqttPublisher &operator=(const MqttPublisher &) = delete;

        /** Construct the client. Call once from setup(), before the task runs. */
        void begin();

        /** (Re)initialise the client with `config`; idempotent, no re-allocation. */
        void connect(const Config::MqttConfig &config);

        /** Apply a new broker configuration at runtime. */
        void updateConfig(const Config::MqttConfig &config) const;

        /**
         * Once-per-second work: failure bridging, keepalive/reconnect, and a
         * publish when the interval has elapsed. `epoch` is the NTP time to
         * stamp on the payloads (0 when unsynced).
         */
        void tick(uint32_t nowMs, uint32_t bootMs, uint32_t epoch, InternetHealth &health);

        void publishMeasurements(const std::vector<Sensor::Measurement> &measurements, uint32_t epoch) const;

        /** WiFi came back: restart the publish timer so there is no burst. */
        void onWifiReconnected(uint32_t nowMs) { lastPublishMs = nowMs; }

        /**
         * Drop back to the base reconnect backoff so MQTT returns within
         * seconds instead of the minutes suspended attempts escalated to.
         */
        void resetBackoff();

        /** nullptr until begin() has run. */
        MqttClient *client() { return mqtt.get(); }

    private:
        SensorController &sensors;
        DarkModeStatusLed &statusLed;
        std::unique_ptr<MqttClient> mqtt;
        uint32_t lastPublishMs = 0;
        uint32_t lastReportedFailures = 0;
    };

} // namespace Net

#endif // KLIMACONTROL_NET_MQTT_PUBLISHER_H
