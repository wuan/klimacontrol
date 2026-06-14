#ifndef KLIMACONTROL_WIFI_H
#define KLIMACONTROL_WIFI_H

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#include "CaptivePortal.h"
#include "StatusLed.h"
#include "MqttClient.h"
#include "sensor/Sensor.h"
#include "SensorController.h"
#include "task/SensorMonitor.h"

// Forward declarations
namespace Config {
    class ConfigManager;
}

class WebServerManager;

/**
 * Network operating modes
 */
enum class NetworkMode {
    STA, // Station mode (WiFi client)
    AP, // Access Point mode (for configuration)
    NONE // Network disabled
};

class Network {
private:
    Config::ConfigManager &config;
    SensorController &sensorController;
    Task::SensorMonitor &sensorMonitor;
    NetworkMode mode;

#ifdef ARDUINO
    WiFiUDP wifiUdp;
    NTPClient ntpClient;
#endif

    std::unique_ptr<WebServerManager> webServer;
    std::unique_ptr<StatusLed> statusLed;
    std::unique_ptr<MqttClient> mqttClient;
    uint32_t lastMqttPublish;
    CaptivePortal captivePortal;
    TaskHandle_t taskHandle = nullptr;
    String mdnsInstanceName;  // Must outlive MDNS.setInstanceName() call
    String cachedHostname;

    // NTP sync state. NTPClient::getEpochTime() returns elapsed-since-boot before any
    // successful sync (because _currentEpoc is 0 and millis-since-_lastUpdate accumulates),
    // so "epoch > 0" is not a reliable synced indicator. Track it explicitly instead.
    bool ntpSynced = false;
    uint32_t lastNtpUpdateEpoch = 0; // epoch seconds at last successful sync

    // WiFi connection state — written from the WiFi event task, read from the network task.
    // 32-bit aligned scalars are atomic on ESP32, so volatile is sufficient.
    volatile uint8_t lastWifiDisconnectReason = 0; // esp_wifi_types reason code
    volatile unsigned long lastWifiConnectMs = 0;
    volatile unsigned long lastWifiDisconnectMs = 0;
    unsigned long lastActiveReconnectMs = 0; // network-task-local
    uint8_t activeReconnectFailures = 0;     // network-task-local
    bool wifiEventHandlerRegistered = false; // WiFi.onEvent registered only once

    // Internet connectivity tracking - shared with MQTT and OTA
    // 32-bit aligned volatile for atomic access on ESP32
    volatile uint32_t internetConnectFailures = 0;
    uint32_t lastInternetFailureAction = 0;
    static constexpr uint32_t INTERNET_FAILURE_THRESHOLD = 5;
    static constexpr uint32_t INTERNET_FAILURE_WINDOW_MS = 60000;

#ifdef ARDUINO
    void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
#endif

    /**
     * Generate mDNS hostname from device ID
     * Creates hostname like "klima-aabbcc" from device ID (removes dash)
     */
    String generateHostname();

    /**
     * Start Access Point mode for configuration
     */
    void startAP();

    /**
     * Start Station mode (WiFi client)
     * @param ssid WiFi network name
     * @param password WiFi password
     */
    void startSTA(const char *ssid, const char *password);

    void configureUsingAPMode();

    /**
     * Configure mDNS responder with hostname and HTTP service advertisement
     */
    void configureMDNS();

public:
    /**
     * Network constructor
     * @param config Configuration manager reference
     * @param sensorController Sensor controller reference
     */
    Network(Config::ConfigManager &config, SensorController &sensorController, Task::SensorMonitor &sensorMonitor);

    // disable copy constructor
    Network(const Network &) = delete;

    /**
     * Network task (runs on Core 1)
     */
    [[noreturn]] void task();

    void startTask();

    /**
     * Static trampoline function for FreeRTOS
     */
    static void taskWrapper(void *pvParameters);

    /**
     * Get current network mode
     */
    [[nodiscard]] NetworkMode getMode() const { return mode; }

    /**
     * Set status LED state
     * @param state LED state to set
     */
    void setStatusLedState(LedState state);

    /**
     * Get current status LED state
     * @return Current LED state, or OFF if LED is disabled
     */
    LedState getStatusLedState() const;

    /**
     * Get MQTT client (for API access)
     * @return Pointer to MQTT client, or nullptr if not initialized
     */
    MqttClient* getMqttClient() { return mqttClient.get(); }

    /**
     * Publish sensor measurements via MQTT
     */
    void publishMeasurements(const std::vector<Sensor::Measurement>& measurements);

    /**
     * Update MQTT configuration at runtime
     */
    void updateMqttConfig(const Config::MqttConfig& mqttConfig);

    /**
     * Report an internet connectivity failure (called by MQTT, OTA, NTP)
     * Increments failure counter and may trigger WiFi reconnection.
     */
    void reportInternetFailure();

    /**
     * Report successful internet connectivity (called by MQTT, OTA, NTP)
     * Resets failure counter.
     */
    void reportInternetSuccess();

    /**
     * Get current NTP epoch time
     * @return Current epoch time, or 0 if NTP not yet successfully synced
     */
#ifdef ARDUINO
    uint32_t getCurrentEpoch() const { return ntpSynced ? ntpClient.getEpochTime() : 0; }
#else
    uint32_t getCurrentEpoch() const { return 0; }
#endif
};


#endif //KLIMACONTROL_WIFI_H
