#include "MqttClient.h"
#include "DeviceId.h"
#include "network/MqttPayload.h"

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include "Log.h"

static constexpr const char* const TAG = "mqtt";
#endif

MqttClient* MqttClient::callbackInstance = nullptr;

MqttClient::MqttClient()
    : config(), clientId(), configured(false)
#ifdef ARDUINO
      ,
      wifiClient(), mqttClient(wifiClient)
#endif
      ,
      lastConnectAttempt(0) {
    // Stack buffer, no String temporary: getDeviceId() returns a cached
    // const char* and "klima-" + ptr is not a valid String expression.
    char id[24];
    snprintf(id, sizeof(id), "klima-%s", DeviceId::getDeviceId());
    clientId = id;
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
    // The callable PubSubClient holds cannot capture this instance, so the
    // static trampoline reaches it through this pointer.
    callbackInstance = this;
    // Cap TCP connect/read timeout to prevent blocking the network task
    wifiClient.setTimeout(3);       // seconds
    mqttClient.setSocketTimeout(3); // seconds

    // Override PubSubClient's compile-time default of MQTT_MAX_PACKET_SIZE = 256.
    // Returns false if heap allocation for the larger buffer fails — log but proceed
    // (the smaller default still works for most payloads). Track the actual size
    // PubSubClient ended up with so publish failures can be distinguished from
    // "buffer too small" (see mqtt-integration spec → "MQTT TX buffer state is observable").
    if (!mqttClient.setBufferSize(MQTT_BUFFER_SIZE)) {
        ESP_LOGW(TAG, "setBufferSize(%u) failed — running with %u bytes (degraded)", MQTT_BUFFER_SIZE,
                 mqttClient.getBufferSize());
    }
    bufferSize = mqttClient.getBufferSize();
    bufferDegraded = (bufferSize < MQTT_BUFFER_SIZE);
    ESP_LOGI(TAG, "TX buffer: %u bytes (requested %u, degraded=%d)", bufferSize, MQTT_BUFFER_SIZE, bufferDegraded);
#endif

#ifdef ARDUINO
    ESP_LOGI(TAG, "Initialized (enabled=%d, host=%s, prefix=%s)", config.enabled, config.host, config.prefix);
#endif
}

void MqttClient::setConfig(const Config::MqttConfig& mqttConfig) {
#ifdef ARDUINO
    bool wasConnected = mqttClient.connected();
    if (wasConnected) {
        // Turning discovery off removes the device from Home Assistant: an
        // empty retained payload on the discovery topic is the protocol's
        // deletion signal. Done before the link drops, while the topic is
        // still reachable. The topic keys on the device id, so the outgoing
        // prefix is irrelevant here.
        if (config.ha_discovery && !mqttConfig.ha_discovery) {
            publishDiscoveryRemoval();
        }

        // Clean-disconnect half of the availability pair: publish a retained
        // `offline` before tearing the link down, so subscribers (Home
        // Assistant) do not have to wait for the LWT, and a config change
        // (including switching MQTT off) reads as a deliberate goodbye rather
        // than a crash. The prefix used is the outgoing one.
        publishStatus("offline");
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
    ESP_LOGI(TAG, "Config updated (enabled=%d, host=%s, prefix=%s)", config.enabled, config.host, config.prefix);
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
    uint32_t backoff = scaled > MAX_RECONNECT_INTERVAL_MS ? MAX_RECONNECT_INTERVAL_MS : static_cast<uint32_t>(scaled);
    if (now - lastConnectAttempt < backoff) return;
    lastConnectAttempt = now;

    // Stale socket fix: stop WiFiClient before every connect attempt
    wifiClient.stop();

    // Pre-connect TCP with a short timeout (ms) to avoid blocking the
    // network task for 7-15s on the default lwIP SYN retransmit timeout.
    static constexpr int TCP_CONNECT_TIMEOUT_MS = 3000;
    if (!wifiClient.connect(config.host, config.port, TCP_CONNECT_TIMEOUT_MS)) {
        consecutiveConnectFailures++;
        ESP_LOGW(TAG, "TCP connect to %s:%u failed (failures=%u, next retry in %ums)", config.host, config.port,
                 consecutiveConnectFailures, backoff);
        return;
    }

    // TCP is up — now do the MQTT handshake (fast, no TCP wait).
    //
    // Register the Last Will in the same CONNECT packet: a retained `offline`
    // on `{prefix}/status`, so the broker publishes it if this client drops
    // without a clean DISCONNECT (crash, power loss, lost link) and Home
    // Assistant marks the entities unavailable. Both strings are local
    // buffers — PubSubClient serialises them into the packet during connect()
    // and keeps no pointer to them.
    char statusTopic[128];
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", config.prefix);
    static constexpr uint8_t WILL_QOS = 0;
    static constexpr bool WILL_RETAIN = true;
    static constexpr const char* WILL_MESSAGE = "offline";

    bool connected;
    if (config.username[0] != '\0') {
        connected = mqttClient.connect(clientId.c_str(), config.username, config.password, statusTopic, WILL_QOS,
                                       WILL_RETAIN, WILL_MESSAGE);
    } else {
        connected =
            mqttClient.connect(clientId.c_str(), nullptr, nullptr, statusTopic, WILL_QOS, WILL_RETAIN, WILL_MESSAGE);
    }

    if (connected) {
        consecutiveConnectFailures = 0;
        connectCount++;
        ESP_LOGI(TAG, "Connected to %s:%u", config.host, config.port);

        // Announce availability, retained, so a subscriber that attaches after
        // this point still sees the current state. The LWT covers the
        // unclean-drop case; this is the connected half of the pair.
        publishStatus("online");

        // Command channel. Registered here rather than in begin() so a failed
        // connect does not leave a callback attached to a dead client, and
        // subscribed on every connect. Independent of `ha_discovery`: commands
        // are useful with discovery off.
        mqttClient.setCallback(callbackTrampoline);
        char commandTopic[160];
        snprintf(commandTopic, sizeof(commandTopic), "%s/target_temperature/set", config.prefix);
        if (!mqttClient.subscribe(commandTopic, 0)) {
            ESP_LOGW(TAG, "Failed to subscribe to %s", commandTopic);
        }
        snprintf(commandTopic, sizeof(commandTopic), "%s/control_enabled/set", config.prefix);
        if (!mqttClient.subscribe(commandTopic, 0)) {
            ESP_LOGW(TAG, "Failed to subscribe to %s", commandTopic);
        }

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
        ESP_LOGW(TAG, "MQTT handshake failed, rc=%d (failures=%u)", mqttClient.state(), consecutiveConnectFailures);
        wifiClient.stop();
    }
#endif
}

bool MqttClient::publish(const char* topic, const char* payload) {
    return publish(topic, payload, false);
}

bool MqttClient::publish(const char* topic, const char* payload, bool retain) {
#ifdef ARDUINO
    if (!mqttClient.connected()) return false;
    bool ok = mqttClient.publish(topic, payload, retain);
    if (!ok && bufferDegraded) {
        truncatedPublishes++;
        ESP_LOGE(TAG, "MQTT publish likely truncated: topic=%s (buffer_size=%u, requested=%u)", topic, bufferSize,
                 MQTT_BUFFER_SIZE);
    }
    return ok;
#else
    (void)topic;
    (void)payload;
    (void)retain;
    return false;
#endif
}

void MqttClient::callbackTrampoline(char* topic, uint8_t* payload, unsigned int length) {
    if (callbackInstance != nullptr) {
        callbackInstance->handleCommand(topic, payload, length);
    }
}

void MqttClient::handleCommand([[maybe_unused]] char* topic, [[maybe_unused]] uint8_t* payload,
                               [[maybe_unused]] unsigned int length) {
#ifdef ARDUINO
    if (!commandHandler) return;

    // The callback's topic and payload are not NUL-terminated and are only
    // valid for the duration of the call, so copy both into bounded buffers
    // before handing them on.
    char topicBuffer[160];
    strlcpy(topicBuffer, topic, sizeof(topicBuffer));

    char payloadBuffer[64];
    const unsigned int copy = length < sizeof(payloadBuffer) - 1 ? length : sizeof(payloadBuffer) - 1;
    memcpy(payloadBuffer, payload, copy);
    payloadBuffer[copy] = '\0';

    commandHandler(topicBuffer, payloadBuffer);
#endif
}

void MqttClient::publishStatus([[maybe_unused]] const char* payload) {
#ifdef ARDUINO
    if (!mqttClient.connected()) return;
    char statusTopic[128];
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", config.prefix);
    if (!publish(statusTopic, payload, true)) {
        ESP_LOGW(TAG, "Failed to publish '%s' to %s", payload, statusTopic);
    }
#endif
}

void MqttClient::publishDiscoveryRemoval() {
#ifdef ARDUINO
    if (!mqttClient.connected()) return;

    char topic[128];
    Net::formatHaDiscoveryTopic(topic, sizeof(topic), DeviceId::getDeviceId());

    // An empty payload (plength 0), retained: the broker replaces the stored
    // discovery message with nothing, and Home Assistant drops the device.
    if (!publish(topic, "", true)) {
        ESP_LOGW(TAG, "Failed to remove HA discovery (%s)", topic);
    } else {
        ESP_LOGI(TAG, "HA discovery removed (%s)", topic);
    }
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
