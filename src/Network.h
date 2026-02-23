#ifndef LEDZ_WIFI_H
#define LEDZ_WIFI_H

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#include "CaptivePortal.h"
#include "TimerScheduler.h"
#include "TouchController.h"
#include "StatusLed.h"

// Forward declarations
namespace Config {
    class ConfigManager;
}

class WebServerManager;
class SensorController;

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
    std::unique_ptr<WebServerManager> webServer;
    std::unique_ptr<TimerScheduler> timerScheduler;
    std::unique_ptr<TouchController> touchController;
    std::unique_ptr<StatusLed> statusLed;
    CaptivePortal captivePortal;
    TaskHandle_t taskHandle = nullptr;

    /**
     * Generate mDNS hostname from device ID
     * Creates hostname like "ledz-aabbcc" from device ID (removes dash)
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

public:
    /**
     * Network constructor
     * @param config Configuration manager reference
     * @param sensorController Sensor controller reference
     */
    Network(Config::ConfigManager &config, SensorController &sensorController);

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
     * Get timer scheduler (for API access)
     * @return Pointer to timer scheduler, or nullptr if not initialized
     */
    TimerScheduler* getTimerScheduler() { return timerScheduler.get(); }

    /**
     * Get touch controller (for API access)
     * @return Pointer to touch controller, or nullptr if not initialized
     */
    TouchController* getTouchController() { return touchController.get(); }

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


#endif //LEDZ_WIFI_H
