#ifndef KLIMACONTROL_WIFI_H
#define KLIMACONTROL_WIFI_H

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#include "CaptivePortal.h"
#include "DarkModeStatusLed.h"
#include "MqttClient.h"
#include "sensor/Sensor.h"
#include "SensorController.h"
#include "control/TemperatureController.h"
#include "actuator/HeatingActuator.h"
#include "task/SensorMonitor.h"
#include "support/NetworkWatchdog.h"
#include "support/Stats.h"

// Forward declarations
namespace Config {
    class ConfigManager;
}

class WebServerManager;

namespace Display {
    class DisplayManager;
}

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
    // The control loop: read for the actuator tick (output, permission) and
    // told what the relay is actually doing afterwards.
    Control::TemperatureController &temperatureController;

    // Drives the heating valve. Lives on this task rather than the control loop
    // because an unreachable manifold takes seconds to time out, and the Sensor
    // Monitor task feeds a watchdog every second.
    Actuator::HeatingActuator heatingActuator;
    unsigned long lastActuatorTickMs = 0;
    Task::SensorMonitor &sensorMonitor;
    // Per-iteration work duration. Fed from the inner loop with `workMs`
    // (the time from the top of the iteration to the existing DEBUG slow-log
    // check) and read by the 15-min diagnostics line on the network task and
    // by the AsyncTCP task at GET /api/about. See spec `networking` →
    // "Network loop accumulates per-iteration work-duration stats".
    Support::Stats stats;
    // Previous iteration's `workMs`, carried across the `vTaskDelay`
    // boundary so the next sleep can be shortened by however long the
    // last iteration took (mirrors `Task::SensorMonitor`'s
    // `tick - elapsed + WAKE_MARGIN_MS` pattern). See spec `networking` →
    // "Network task sleeps adaptively based on previous iteration's work".
    uint32_t lastWorkMs = 0;
    NetworkMode mode;

#ifdef ARDUINO
    WiFiUDP wifiUdp;
    NTPClient ntpClient;
#endif

    // Long-lived singletons. The web server is constructed once in setup()
    // and the same instance is reused across AP/STA/STA-fallback cycles by
    // calling setMode(). MqttClient is constructed once in `Network::begin()`
    // and re-initialized in place on each (re)connect. See spec
    // `memory-management` → "Long-lived singletons are constructed once".
    WebServerManager *webServer = nullptr;
    // Non-owning; nullptr when the e-paper display is disabled in config (the
    // default) or on native builds. Wired via setDisplay() after construction,
    // the same way webServer is, so neither object needs the other at
    // construction time.
    Display::DisplayManager *display = nullptr;
    DarkModeStatusLed &statusLed;
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
    uint32_t ntpBogusSyncCount = 0;  // syncs that passed the boolean check but failed the epoch sanity check

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

    /**
     * Run ntpClient.forceUpdate() with the task watchdog fed immediately
     * before and after. See `src/support/NetworkWatchdog.h` for the helper
     * and the spec `networking` → "Network task blocking-call safety" for
     * the contract.
     */
    bool safeNtpUpdate();

public:
    /**
     * Network constructor
     * @param config Configuration manager reference
     * @param sensorController Sensor controller reference (MQTT publishing)
     * @param temperatureController Control loop (actuator tick)
     * @param statusLed Status LED (non-owning reference; the LED is created at
     *                  namespace scope in main.cpp so it is available when
     *                  SensorController's constructor runs)
     * @param webServer Pre-constructed WebServerManager (long-lived). The
     *                  Network does not own it; ownership stays with the
     *                  caller (main.cpp). Switched into CONFIG/OPERATIONAL
     *                  mode via setMode() from the network task.
     */
    Network(Config::ConfigManager &config, SensorController &sensorController,
            Control::TemperatureController &temperatureController, Task::SensorMonitor &sensorMonitor,
            DarkModeStatusLed &statusLed, WebServerManager *webServer);

    /** Read-only view of the heating actuator, for the API and displays. */
    const Actuator::HeatingActuator &getHeatingActuator() const { return heatingActuator; }

    /** Ask the actuator to re-read its channel configuration promptly. */
    void requestActuatorRecheck() { heatingActuator.requestRecheck(); }

    /**
     * Take an indivisible copy of the Network-loop cycle counters. Cross-task
     * readers (e.g. GET /api/about on the AsyncTCP task) SHALL go through
     * this accessor rather than the per-field getters — see the
     * "Cross-task reads of `Support::Stats` use a snapshot accessor"
     * requirement in openspec/specs/system-architecture/spec.md.
     */
    Support::StatsSnapshot getStatsSnapshot() const { return stats.snapshot(); }

    // disable copy constructor
    Network(const Network &) = delete;

    /**
     * Wire the pre-constructed WebServerManager into the network task. Called
     * from main.cpp after both objects exist (the WebServerManager constructor
     * needs a Network& reference, so it can't be built first). The pointer is
     * non-owning — main.cpp keeps the WebServerManager alive for the lifetime
     * of the firmware.
     */
    void setWebServer(WebServerManager *webServer) { this->webServer = webServer; }

    /**
     * Wire in the e-paper display, if one is enabled. Non-owning; pass nullptr
     * (or never call this) to leave the display unused.
     */
    void setDisplay(Display::DisplayManager *display) { this->display = display; }

    /**
     * The wired-in display, or nullptr when none is enabled. Non-owning.
     */
    Display::DisplayManager *getDisplay() const { return display; }

    /**
     * One-time initialization of long-lived singletons that the network task
     * depends on. Currently constructs the MqttClient so the same instance is
     * reused across every (re)connect (idempotent `begin()` re-init only).
     * Must be called from setup() before the network task starts.
     */
    void begin();

    /**
     * Network task (runs on Core 1)
     */
    [[noreturn]] void task();

    void startTask();

    /**
     * Size of the AP password buffer: 8 hex chars from
     * `Support::computeApPassword` plus a NUL terminator. Public so
     * main.cpp can size its stack buffer without depending on the
     * private `apPassword` member.
     */
    static constexpr size_t AP_PASSWORD_BUF_SIZE = 9;

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
     * Apply a new status-LED dark-mode threshold live (no restart). Safe to
     * call from the HTTP handler task; the LED stores it atomically.
     * @param seconds Seconds of normal operation before the LED goes dark; 0 disables
     */
    void setLedDarkAfterSeconds(uint16_t seconds);

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
