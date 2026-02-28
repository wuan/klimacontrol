#ifndef KLIMACONTROL_MQTT_CLIENT_H
#define KLIMACONTROL_MQTT_CLIENT_H

#include "Config.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <PubSubClient.h>
#endif

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
};

#endif // KLIMACONTROL_MQTT_CLIENT_H
