#include "MqttClient.h"
#include "DeviceId.h"

#ifdef ARDUINO
#include <Arduino.h>
#include "Log.h"

static const char* TAG = "mqtt";
#endif

MqttClient::MqttClient()
    : config(), clientId("klima-" + DeviceId::getDeviceId()), configured(false)
#ifdef ARDUINO
      , wifiClient(), mqttClient(wifiClient)
#endif
      , lastConnectAttempt(0)
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

    // Override PubSubClient's compile-time default of MQTT_MAX_PACKET_SIZE = 256.
    // Returns false if heap allocation for the larger buffer fails — log but proceed
    // (the smaller default still works for most payloads). Track the actual size
    // PubSubClient ended up with so publish failures can be distinguished from
    // "buffer too small" (see mqtt-integration spec → "MQTT TX buffer state is observable").
    if (!mqttClient.setBufferSize(MQTT_BUFFER_SIZE)) {
        ESP_LOGW(TAG, "setBufferSize(%u) failed — running with %u bytes (degraded)",
                 MQTT_BUFFER_SIZE, mqttClient.getBufferSize());
    }
    bufferSize = mqttClient.getBufferSize();
    bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE);
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
    consecutiveConnectFailures = 0; // fresh config — start at base backoff
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

    // Reconnect with exponential backoff. Cap shift so RECONNECT_INTERVAL_MS << shift
    // can't undefined-behavior overflow uint32_t; the std::min against the cap handles
    // the actual ceiling.
    uint32_t now = millis();
    uint32_t shift = consecutiveConnectFailures < 16 ? consecutiveConnectFailures : 16;
    uint64_t scaled = static_cast<uint64_t>(RECONNECT_INTERVAL_MS) << shift;
    uint32_t backoff = scaled > MAX_RECONNECT_INTERVAL_MS
        ? MAX_RECONNECT_INTERVAL_MS
        : static_cast<uint32_t>(scaled);
    if (now - lastConnectAttempt < backoff) return;
    lastConnectAttempt = now;

    // Stale socket fix: stop WiFiClient before every connect attempt
    wifiClient.stop();

    // Pre-connect TCP with a short timeout (ms) to avoid blocking the
    // network task for 7-15s on the default lwIP SYN retransmit timeout.
    static constexpr int TCP_CONNECT_TIMEOUT_MS = 3000;
    if (!wifiClient.connect(config.host, config.port, TCP_CONNECT_TIMEOUT_MS)) {
        consecutiveConnectFailures++;
        ESP_LOGW(TAG, "TCP connect to %s:%u failed (failures=%u, next retry in %ums)",
                 config.host, config.port, consecutiveConnectFailures, backoff);
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
        consecutiveConnectFailures = 0;
        ESP_LOGI(TAG, "Connected to %s:%u", config.host, config.port);

        // Opportunistic buffer recovery: a transient allocation failure at
        // boot may resolve now that the heap is slightly less fragmented.
        // Only log on the false→healthy transition to avoid log spam.
        bool wasDegraded = bufferDegraded;
        mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
        bufferSize = mqttClient.getBufferSize();
        bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE);
        if (wasDegraded && !bufferDegraded) {
            ESP_LOGI(TAG, "MQTT buffer recovered: %u bytes", bufferSize);
        }
    } else {
        consecutiveConnectFailures++;
        ESP_LOGW(TAG, "MQTT handshake failed, rc=%d (failures=%u)",
                 mqttClient.state(), consecutiveConnectFailures);
        wifiClient.stop();
    }
#endif
}

bool MqttClient::publish([[maybe_unused]] const char* topic, [[maybe_unused]] const char* payload) {
#ifdef ARDUINO
    if (!mqttClient.connected()) return false;
    bool ok = mqttClient.publish(topic, payload);
    if (!ok && bufferDegraded) {
        truncatedPublishes++;
        ESP_LOGE(TAG, "MQTT publish likely truncated: topic=%s (buffer_size=%u, requested=%u)",
                 topic, bufferSize, MQTT_BUFFER_SIZE);
    }
    return ok;
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
