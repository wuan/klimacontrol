#include "MqttClient.h"
#include "DeviceId.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "Log.h"
#endif

static const char* TAG = "mqtt";

MqttClient::MqttClient()
    : clientId("klima-" + DeviceId::getDeviceId()), configured(false), lastConnectAttempt(0)
#ifdef ARDUINO
      , mqttClient(wifiClient)
#endif
{
}

void MqttClient::applyServer() {
#ifdef ARDUINO
    if (config.host[0] == '\0') return;

    if (isIpAddress(config.host)) {
        // Use IPAddress overload — stores IP by value, avoids DNS lookup
        IPAddress ip;
        if (ip.fromString(config.host)) {
            mqttClient.setServer(ip, config.port);
            ESP_LOGI(TAG, "Server set to IP %s:%u", config.host, config.port);
        }
    } else {
        // Use const char* overload — must point to long-lived member (dangling pointer fix)
        mqttClient.setServer(this->config.host, config.port);
        ESP_LOGI(TAG, "Server set to hostname %s:%u", config.host, config.port);
    }
#endif
}

void MqttClient::begin(const Config::MqttConfig& mqttConfig) {
    config = mqttConfig;
    configured = true;
    applyServer();

#ifdef ARDUINO
    // Cap TCP connect/read timeout to prevent blocking the network task
    wifiClient.setTimeout(3);  // seconds
    mqttClient.setSocketTimeout(3);  // seconds
#endif

#ifdef ARDUINO
    ESP_LOGI(TAG, "Initialized (enabled=%d, host=%s, prefix=%s)",
             config.enabled, config.host, config.prefix);
#endif
}

void MqttClient::setConfig(const Config::MqttConfig& mqttConfig) {
#ifdef ARDUINO
    bool wasConnected = mqttClient.connected();
    if (wasConnected) {
        mqttClient.disconnect();
        wifiClient.stop();
    }
#endif

    config = mqttConfig;
    configured = true;
    lastConnectAttempt = 0;
    applyServer();

#ifdef ARDUINO
    ESP_LOGI(TAG, "Config updated (enabled=%d, host=%s, prefix=%s)",
             config.enabled, config.host, config.prefix);
#endif
}

void MqttClient::loop() {
#ifdef ARDUINO
    if (!configured || !config.enabled || config.host[0] == '\0') return;

    if (mqttClient.connected()) {
        mqttClient.loop();
        return;
    }

    // Reconnect with backoff
    uint32_t now = millis();
    if (now - lastConnectAttempt < RECONNECT_INTERVAL_MS) return;
    lastConnectAttempt = now;

    // Stale socket fix: stop WiFiClient before every connect attempt
    wifiClient.stop();

    // Pre-connect TCP with a short timeout (ms) to avoid blocking the
    // network task for 7-15s on the default lwIP SYN retransmit timeout.
    static constexpr int TCP_CONNECT_TIMEOUT_MS = 3000;
    if (!wifiClient.connect(config.host, config.port, TCP_CONNECT_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "TCP connect to %s:%u failed", config.host, config.port);
        return;
    }

    // TCP is up — now do the MQTT handshake (fast, no TCP wait)
    bool connected;
    if (config.username[0] != '\0') {
        connected = mqttClient.connect(clientId.c_str(), config.username, config.password);
    } else {
        connected = mqttClient.connect(clientId.c_str());
    }

    if (connected) {
        ESP_LOGI(TAG, "Connected to %s:%u", config.host, config.port);
    } else {
        ESP_LOGW(TAG, "MQTT handshake failed, rc=%d", mqttClient.state());
        wifiClient.stop();
    }
#endif
}

bool MqttClient::publish(const char* topic, const char* payload) {
#ifdef ARDUINO
    if (!mqttClient.connected()) return false;
    return mqttClient.publish(topic, payload);
#else
    return false;
#endif
}

bool MqttClient::isConnected() {
#ifdef ARDUINO
    return mqttClient.connected();
#else
    return false;
#endif
}

bool MqttClient::isEnabled() const {
    return configured && config.enabled;
}

void MqttClient::recordPublishResult(uint32_t succeeded, uint32_t failed) {
    publishedCount += succeeded;
    failedCount += failed;
    publishCycles++;
    if (failed > 0) failedCycles++;
}
