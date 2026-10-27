#ifndef KLIMACONTROL_MQTT_CLIENT_H
#define KLIMACONTROL_MQTT_CLIENT_H

#include <functional>

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
    Config::MqttConfig config;
    String clientId;
    bool configured;
#ifdef ARDUINO
    WiFiClient wifiClient;
    PubSubClient mqttClient;
#endif
    uint32_t lastConnectAttempt;
    uint32_t consecutiveConnectFailures = 0;
    static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;       // initial backoff
    static constexpr uint32_t MAX_RECONNECT_INTERVAL_MS = 300000; // cap at 5 minutes

    // Publish statistics
    uint32_t publishedCount = 0;     // successful individual publishes
    uint32_t failedCount = 0;        // failed individual publishes
    uint32_t publishCycles = 0;      // total publish cycles (calls to publishAll)
    uint32_t failedCycles = 0;       // cycles with at least one failure
    uint32_t truncatedPublishes = 0; // publishes that failed while buffer was degraded

    // Successful connects, ever. MqttPublisher compares it across ticks to
    // detect a fresh (re)connect and run the event-driven publishes (retained
    // availability, Home Assistant discovery, a full control-state refresh)
    // exactly once per connection, outside the measurement cadence.
    uint32_t connectCount = 0;

    // TX buffer state — see spec mqtt-integration → "MQTT TX buffer state is observable"
    uint16_t bufferSize = 0;     // actual buffer size PubSubClient is using
    bool bufferDegraded = false; // true when bufferSize < MQTT_BUFFER_SIZE

    void applyServer();

    /**
     * Publish `payload` retained to `{prefix}/status` — the availability topic
     * the LWT and the Home Assistant discovery payload both refer to.
     */
    void publishStatus(const char* payload);

    /**
     * Publish an empty retained payload to this device's Home Assistant
     * discovery topic, which removes the device from Home Assistant. Called
     * when `ha_discovery` is switched off while connected.
     */
    void publishDiscoveryRemoval();

    /**
     * Inbound-command sink. `setCallback()` takes a plain callable, not a
     * member function, so the instance is reached through the static
     * `callbackInstance` trampoline.
     */
    using CommandHandler = std::function<void(const char* topic, const char* payload)>;
    CommandHandler commandHandler;

    /** The instance the C trampoline dispatches to; set in begin(). */
    static MqttClient* callbackInstance;
    static void callbackTrampoline(char* topic, uint8_t* payload, unsigned int length);

    /** Copy the callback's non-terminated buffers and hand them to the sink. */
    void handleCommand(char* topic, uint8_t* payload, unsigned int length);

public:
    // PubSubClient's compile-time default (256) is far too tight for our
    // publishes. Two payloads shape the size: the sensor measurement JSON
    // (well under 256 B) and, much larger, the retained Home Assistant
    // device-discovery payload, which is ~1.35 kB typical and ~1.5 kB with the
    // longest permitted prefix and device name. 1024 truncated that discovery
    // message; 2048 leaves headroom and is pinned by the worst-case size test
    // in test/test_mqtt_payload. The allocation is made once per connect, and
    // getBufferSize() reports what PubSubClient actually ended up with.
    static constexpr uint16_t MQTT_BUFFER_SIZE = 2048;

    // PubSubClient rejects a publish when its buffer cannot hold
    // MQTT_MAX_HEADER_SIZE (5) + the 2-byte topic-length field + the topic +
    // the payload. Exposed so the discovery size test asserts against the same
    // framing the library enforces rather than a restated magic number.
    static constexpr uint16_t MQTT_FRAMING_OVERHEAD = 7;

    MqttClient();
    void begin(const Config::MqttConfig& config);
    void setConfig(const Config::MqttConfig& config);
    void loop();
    bool publish(const char* topic, const char* payload);
    /**
     * Publish with an explicit retain flag. QoS is still 0 — PubSubClient
     * exposes no QoS parameter, see the mqtt-integration spec. The degraded/
     * healthy accounting is identical to the two-argument form: a publish that
     * fails while the buffer is below MQTT_BUFFER_SIZE counts as truncated.
     */
    bool publish(const char* topic, const char* payload, bool retain);
    void setCommandHandler(CommandHandler handler) { commandHandler = std::move(handler); }
    bool isConnected();
    bool isEnabled() const;
    uint32_t getIntervalMs() const { return static_cast<uint32_t>(config.interval) * 1000; }
    const char* getPrefix() const { return config.prefix; }

    // Publish statistics
    uint32_t getPublishedCount() const { return publishedCount; }
    uint32_t getFailedCount() const { return failedCount; }
    uint32_t getPublishCycles() const { return publishCycles; }
    uint32_t getFailedCycles() const { return failedCycles; }
    uint32_t getTruncatedPublishes() const { return truncatedPublishes; }
    uint32_t getConnectCount() const { return connectCount; }
    /** Whether the runtime config has Home Assistant discovery enabled. */
    bool isHaDiscoveryEnabled() const { return configured && config.enabled && config.ha_discovery; }

    // TX buffer state (see mqtt-integration spec)
    uint16_t getBufferSize() const { return bufferSize; }
    bool isBufferDegraded() const { return bufferDegraded; }
    uint32_t getConsecutiveConnectFailures() const { return consecutiveConnectFailures; }
    void recordPublishResult(uint32_t succeeded, uint32_t failed);
    void resetConsecutiveConnectFailures() { consecutiveConnectFailures = 0; }
};

#endif // KLIMACONTROL_MQTT_CLIENT_H
