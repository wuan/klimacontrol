#ifndef KLIMACONTROL_WIFI_H
#define KLIMACONTROL_WIFI_H

#include <cstdint>
#include <vector>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include "DarkModeStatusLed.h"
#include "MqttClient.h"
#include "actuator/HeatingActuator.h"
#include "control/TemperatureController.h"
#include "network/ApProvisioning.h"
#include "network/InternetHealth.h"
#include "network/LowHeapGuard.h"
#include "network/MdnsAdvertiser.h"
#include "network/MqttPublisher.h"
#include "network/NtpSync.h"
#include "network/WifiStation.h"
#include "sensor/Sensor.h"
#include "support/Stats.h"
#include "task/SensorMonitor.h"

namespace Config {
    class ConfigManager;
}

class SensorController;
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

/**
 * The Network task: boots the device onto WiFi (or into AP provisioning),
 * then runs the 1 s housekeeping loop — status LED, heating actuator, e-paper
 * refresh, WiFi supervision, NTP, MQTT, internet-health recovery, low-heap
 * guard and diagnostics. The individual concerns live in `src/network/`;
 * this class owns them and sequences their ticks.
 */
class Network {
    Config::ConfigManager &config;
    // The control loop: read for the actuator tick (output, permission) and
    // told what the relay is actually doing afterwards.
    Control::TemperatureController &temperatureController;
    Task::SensorMonitor &sensorMonitor;
    DarkModeStatusLed &statusLed;

    // Drives the heating valve. Lives on this task rather than the control loop
    // because an unreachable manifold takes seconds to time out, and the Sensor
    // Monitor task feeds a watchdog every second.
    Actuator::HeatingActuator heatingActuator;
    uint32_t lastActuatorTickMs = 0;

    Net::MdnsAdvertiser mdns;
    Net::ApProvisioning provisioning;
    Net::WifiStation wifi;
    Net::NtpSync ntp;
    Net::MqttPublisher mqtt;
    Net::InternetHealth internetHealth;
    Net::LowHeapGuard lowHeapGuard;

    Support::Stats stats;
    uint32_t lastElapsedMs = 0;
    NetworkMode mode = NetworkMode::NONE;

    std::optional<std::reference_wrapper<WebServerManager>> webServer;
    std::optional<std::reference_wrapper<Display::DisplayManager>> display;
#ifdef ARDUINO
    TaskHandle_t taskHandle = nullptr;
#endif

    /**
     * Boot-time station bring-up: associate, then start mDNS, NTP and MQTT.
     * Returns false when association failed after all retries.
     */
    bool startSTA(const char *ssid, const char *password);

    /** Heating actuator tick, rate-limited to HeatingActuator::TICK_MS. */
    void tickActuator(uint32_t now);

    /** 15-minute heap / cycle-stats / stack-HWM log lines. */
    void logDiagnostics(uint32_t now);

    void initialize_wifi(const uint8_t &AP_FALLBACK_THRESHOLD);

    void handle_connection_failure(const uint8_t &AP_FALLBACK_THRESHOLD);

    void handle_network_events(uint32_t now);

    static void initialize_watchdog_timer();

    void enable_webserver();

public:
    /**
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
            DarkModeStatusLed &statusLed, std::optional<std::reference_wrapper<WebServerManager>> webServer);

    // disable copy constructor
    Network(const Network &) = delete;

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

    /**
     * Wire the pre-constructed WebServerManager into the network task. Called
     * from main.cpp after both objects exist (the WebServerManager constructor
     * needs a Network& reference, so it can't be built first). The pointer is
     * non-owning — main.cpp keeps the WebServerManager alive for the lifetime
     * of the firmware.
     */
    void setWebServer(WebServerManager& webServer);

    /**
     * Wire in the e-paper display, if one is enabled. Non-owning; pass nullptr
     * (or never call this) to leave the display unused.
     */
    void setDisplay(Display::DisplayManager& display);

    /**
     * The wired-in display, or nullptr when none is enabled. Non-owning.
     */
    std::optional<std::reference_wrapper<Display::DisplayManager>> getDisplay() const { return display; }

    /**
     * One-time initialization of long-lived singletons that the network task
     * depends on (currently the MqttClient, so the same instance is reused
     * across every (re)connect). Must be called from setup() before the
     * network task starts.
     */
    void begin();

    /**
     * Network task body
     */
    [[noreturn]] void task();

    void startTask();

    /**
     * Size of the AP password buffer: 8 hex chars from
     * `Support::computeApPassword` plus a NUL terminator.
     */
    static constexpr size_t AP_PASSWORD_BUF_SIZE = Net::ApProvisioning::AP_PASSWORD_BUF_SIZE;

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
    MqttClient *getMqttClient() { return mqtt.client(); }

    /**
     * Publish sensor measurements via MQTT
     */
    void publishMeasurements(const std::vector<Sensor::Measurement> &measurements);

    /**
     * Update MQTT configuration at runtime
     */
    void updateMqttConfig(const Config::MqttConfig &mqttConfig);

    /**
     * Report an internet connectivity failure (called by MQTT, OTA, NTP)
     * Increments failure counter and may trigger WiFi reconnection.
     */
    void reportInternetFailure() { internetHealth.reportFailure(); }

    /**
     * Report successful internet connectivity (called by MQTT, OTA, NTP)
     * Resets failure counter.
     */
    void reportInternetSuccess() { internetHealth.reportSuccess(); }

    /**
     * Get current NTP epoch time
     * @return Current epoch time, or 0 if NTP not yet successfully synced
     */
    uint32_t getCurrentEpoch() const { return ntp.currentEpoch(); }
};


#endif //KLIMACONTROL_WIFI_H
