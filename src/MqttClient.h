#ifndef KLIMACONTROL_MQTT_CLIENT_H
#define KLIMACONTROL_MQTT_CLIENT_H

#include "Config.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <PubSubClient.h>
#endif

/**
 * Check if a string looks like an IPv4 address (digits and dots only)
 */
inline bool isIpAddress(const char* host) {
    if (!host || host[0] == '\0') return false;
    for (const char* p = host; *p; p++) {
        if (*p != '.' && (*p < '0' || *p > '9')) {
            return false;
        }
    }
    return true;
}

class MqttClient {
private:
#ifdef ARDUINO
    WiFiClient wifiClient;
    PubSubClient mqttClient;
#endif
    Config::MqttConfig config;
    String clientId;
    bool configured;
    uint32_t lastConnectAttempt;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;

    // Publish statistics
    uint32_t publishedCount = 0;    // successful individual publishes
    uint32_t failedCount = 0;       // failed individual publishes
    uint32_t publishCycles = 0;     // total publish cycles (calls to publishAll)
    uint32_t failedCycles = 0;      // cycles with at least one failure

    void applyServer();

public:
    MqttClient();
    void begin(const Config::MqttConfig& config);
    void setConfig(const Config::MqttConfig& config);
    void loop();
    bool publish(const char* topic, const char* payload);
    bool isConnected();
    bool isEnabled() const;
    uint32_t getIntervalMs() const { return static_cast<uint32_t>(config.interval) * 1000; }
    const char* getPrefix() const { return config.prefix; }

    // Publish statistics
    uint32_t getPublishedCount() const { return publishedCount; }
    uint32_t getFailedCount() const { return failedCount; }
    uint32_t getPublishCycles() const { return publishCycles; }
    uint32_t getFailedCycles() const { return failedCycles; }
    void recordPublishResult(uint32_t succeeded, uint32_t failed);
};

#endif // KLIMACONTROL_MQTT_CLIENT_H
