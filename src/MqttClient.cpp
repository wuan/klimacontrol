#include "MqttClient.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

MqttClient::MqttClient()
    : configured(false), lastConnectAttempt(0)
#ifdef ARDUINO
      , mqttClient(wifiClient)
#endif
{
}

void MqttClient::applyServer() {
#ifdef ARDUINO
    if (config.host[0] == '\0') return;

    // Check if host is an IP address (all digits and dots)
    bool isIp = true;
    for (const char* p = config.host; *p; p++) {
        if (*p != '.' && (*p < '0' || *p > '9')) {
            isIp = false;
            break;
        }
    }

    if (isIp) {
        // Use IPAddress overload — stores IP by value, avoids DNS lookup
        IPAddress ip;
        if (ip.fromString(config.host)) {
            mqttClient.setServer(ip, config.port);
            Serial.printf("MQTT: Server set to IP %s:%u\n", config.host, config.port);
        }
    } else {
        // Use const char* overload — must point to long-lived member (dangling pointer fix)
        mqttClient.setServer(this->config.host, config.port);
        Serial.printf("MQTT: Server set to hostname %s:%u\n", config.host, config.port);
    }
#endif
}

void MqttClient::begin(const Config::MqttConfig& mqttConfig) {
    config = mqttConfig;
    configured = true;
    applyServer();

#ifdef ARDUINO
    Serial.printf("MQTT: Initialized (enabled=%d, host=%s, prefix=%s)\n",
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
    Serial.printf("MQTT: Config updated (enabled=%d, host=%s, prefix=%s)\n",
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

    Serial.println("MQTT: Connecting...");

    // Stale socket fix: stop WiFiClient before every connect attempt
    wifiClient.stop();

    bool connected;
    if (config.username[0] != '\0') {
        connected = mqttClient.connect("klimacontrol", config.username, config.password);
    } else {
        connected = mqttClient.connect("klimacontrol");
    }

    if (connected) {
        Serial.println("MQTT: Connected");
    } else {
        Serial.printf("MQTT: Connect failed, rc=%d\n", mqttClient.state());
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
