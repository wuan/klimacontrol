#ifndef KLIMACONTROL_WIFI_H
#define KLIMACONTROL_WIFI_H

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_NeoPixel.h>
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
    NetworkMode mode;

#ifdef ARDUINO
    WiFiUDP wifiUdp;
    NTPClient ntpClient;
#endif

    Config::ConfigManager &config;
    SensorController &sensorController;
    Task::SensorMonitor &sensorMonitor;
    std::unique_ptr<WebServerManager> webServer;
    std::unique_ptr<StatusLed> statusLed;
    std::unique_ptr<MqttClient> mqttClient;
    uint32_t lastMqttPublish;
    CaptivePortal captivePortal;
    TaskHandle_t taskHandle = nullptr;

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
     * Set WiFi TX power at runtime (takes effect immediately if in STA mode)
     * @param power Raw wifi_power_t value (see WiFiConfig TX_POWER_* constants)
     */
    void setWifiTxPower(int8_t power);

    /**
     * Get current NTP epoch time
     * @return Current epoch time, or 0 if NTP not available
     */
#ifdef ARDUINO
    uint32_t getCurrentEpoch() const { return ntpClient.getEpochTime(); }
#else
    uint32_t getCurrentEpoch() const { return 0; }
#endif
};


#endif //KLIMACONTROL_WIFI_H
