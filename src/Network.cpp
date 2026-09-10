#include "Network.h"

#include "Config.h"
#include "Constants.h"
#include "Log.h"
#include "OTAUpdater.h"
#include "SensorController.h"
#include "SyslogOutput.h"
#include "WebServerManager.h"
#include "support/WifiBackoff.h"

#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include "display/DisplayManager.h"
#endif

static constexpr const char *const TAG = "net";

Network::Network(Config::ConfigManager &config, SensorController &sensorController,
                 Control::TemperatureController &temperatureController, Task::SensorMonitor &sensorMonitor,
                 DarkModeStatusLed &statusLed, WebServerManager *webServer)
    : config(config), temperatureController(temperatureController),
      sensorMonitor(sensorMonitor), statusLed(statusLed),
      mdns(config), provisioning(config, mdns), wifi(config), mqtt(sensorController, statusLed),
      webServer(webServer) {
    if (webServer != nullptr) {
        provisioning.setWebServer(*webServer);
    }
}

void Network::begin() {
    mqtt.begin();
}

void Network::setWebServer(WebServerManager *server) {
    webServer = server;
    if (server != nullptr) {
        provisioning.setWebServer(*server);
    } else {
        provisioning.clearWebServer();
    }
}

void Network::setDisplay(Display::DisplayManager *displayManager) {
    display = displayManager;
    if (displayManager != nullptr) {
        provisioning.setDisplay(*displayManager);
    } else {
        provisioning.clearDisplay();
    }
}

void Network::setStatusLedState(LedState state) {
    statusLed.setState(state);
}

LedState Network::getStatusLedState() const {
    return statusLed.getState();
}

void Network::setLedDarkAfterSeconds(uint16_t seconds) {
    statusLed.setDarkAfterSeconds(seconds);
}

void Network::publishMeasurements(const std::vector<Sensor::Measurement> &measurements) {
    mqtt.publishMeasurements(measurements, ntp.currentEpoch());
}

void Network::updateMqttConfig(const Config::MqttConfig &mqttConfig) {
    mqtt.updateConfig(mqttConfig);
}

bool Network::startSTA(const char *ssid, const char *password) {
    mode = NetworkMode::STA;

    if (!wifi.connect(ssid, password)) return false;

#ifdef ARDUINO
    ESP_LOGI(TAG, "Configuring mDNS...");
    mdns.advertise();
    ESP_LOGI(TAG, "%s available at http://%s.local/ or http://%s",
             Constants::PROJECT_NAME, mdns.hostname().c_str(), WiFi.localIP().toString().c_str());
#endif

    ntp.begin();
    mqtt.connect(config.loadMqttConfig());
    return true;
}

void Network::tickActuator(uint32_t now) {
    if (now - lastActuatorTickMs < Actuator::HeatingActuator::TICK_MS) return;
    lastActuatorTickMs = now;
    const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();
    heatingActuator.configure(cfg);
    heatingActuator.tick(temperatureController.getControlOutput(),
                         temperatureController.isHeatingPermitted(), now);
    temperatureController.publishActuatorState(heatingActuator.isAssigned(),
                                               heatingActuator.agreement(now));
}

void Network::logDiagnostics(uint32_t now) {
#ifdef ARDUINO
    ESP_LOGI(TAG, "Diagnostics: heap=%u bytes (min=%u), uptime=%lu s",
             ESP.getFreeHeap(), ESP.getMinFreeHeap(), static_cast<unsigned long>(now / 1000));
    const Support::StatsSnapshot netStats = stats.snapshot();
    ESP_LOGI(TAG,
             "Diagnostics: net_cycle_count=%llu net_avg_cycle_work_ms=%llu net_min_cycle_work_ms=%llu net_max_cycle_work_ms=%llu",
             (unsigned long long) netStats.count,
             (unsigned long long) netStats.average,
             (unsigned long long) netStats.min,
             (unsigned long long) netStats.max);
    if (taskHandle) {
        ESP_LOGI(TAG, "Network task stack HWM: %u bytes",
                 uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t));
    }
#else
    (void) now;
#endif
}

void Network::initialize_wifi(const uint8_t &AP_FALLBACK_THRESHOLD) {
    if (!config.isConfigured()) {
        ESP_LOGI(TAG, "No WiFi configuration found - starting AP mode");
        mode = NetworkMode::AP;
        provisioning.runFirstBoot();
    }

    const uint8_t failures = config.getConnectionFailures();
    ESP_LOGI(TAG, "Previous connection failures: %u", failures);
    if (failures > 0 && failures % AP_FALLBACK_THRESHOLD == 0) {
        mode = NetworkMode::AP;
        provisioning.runFallbackWindow(failures);
    }
}

void Network::handle_connection_failure(const uint8_t &AP_FALLBACK_THRESHOLD) {
    // incrementConnectionFailures() already persists wifi_failures to NVS.
    const uint8_t newFailures = config.incrementConnectionFailures();

    // Doubling backoff (capped at 5 min) instead of a fixed 2 s delay so a
    // transient AP outage has room to recover before the device tears down
    // its association state. See the spec `network-wifi-resilience` →
    // "Exponential backoff on boot-time STA failure".
    const uint32_t backoffMs = Support::staFailureBackoffMs(newFailures);
    ESP_LOGW(TAG, "Failed to connect (failure %u/%u) - waiting %u ms before retry...",
             newFailures, AP_FALLBACK_THRESHOLD, backoffMs);

    vTaskDelay(backoffMs / portTICK_PERIOD_MS);
    ESP.restart();
}

void Network::handle_network_events(const uint32_t now) {
    switch (wifi.supervise(now)) {
        case Net::WifiStation::Event::Reconnected:
            mdns.advertise();
            mqtt.onWifiReconnected(now);
            break;
        case Net::WifiStation::Event::RestartRequired:
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP.restart();
            break;
        case Net::WifiStation::Event::None:
            break;
    }
}

[[noreturn]] void Network::task() {
#ifdef ARDUINO
    const uint32_t bootMs = millis(); // baseline for boot-relative checks (wrap-safe via subtraction)

    initialize_watchdog_timer();

    ESP_LOGI(TAG, "Network task started");

    statusLed.begin(bootMs, config.loadEnergyConfig().led_dark_after_s);

    static constexpr uint8_t AP_FALLBACK_THRESHOLD = 3;
    initialize_wifi(AP_FALLBACK_THRESHOLD);

    ESP_LOGI(TAG, "Network task configured");

    const Config::WiFiConfig wifiConfig = config.loadWiFiConfig();
    ESP_LOGI(TAG, "WiFi configured - starting STA mode");
    if (!startSTA(wifiConfig.ssid, wifiConfig.password)) {
        handle_connection_failure(AP_FALLBACK_THRESHOLD);
    }

    config.resetConnectionFailures();

    statusLed.setState(LedState::ON); // Solid on for connected state

    enable_webserver();

    // Set syslog hostname early so it's available if syslog is enabled later via API
    SyslogOutput::setHostname(mdns.hostname().c_str());
    SyslogOutput::begin(config.loadSyslogConfig());

    uint32_t lastDiagnostics = millis();
    bool otaWasActive = false;
    wifi.beginSupervision(millis());

    static constexpr uint32_t DIAGNOSTICS_INTERVAL_MS = 900000; // 15 minutes
    static constexpr uint32_t LOOP_TICKS_MS = 1000;
    static constexpr uint32_t WAKE_MARGIN_MS = 2;

    while (true) {
        if (lastElapsedMs < LOOP_TICKS_MS) {
            const uint32_t sleepMs = (LOOP_TICKS_MS - lastElapsedMs + WAKE_MARGIN_MS);
            vTaskDelay(pdMS_TO_TICKS(sleepMs));
        }

        esp_task_wdt_reset();

        const uint32_t startTime = millis();

        const bool otaActive = OTAUpdater::isUpdateInProgress();

        statusLed.update(startTime);

        handle_network_events(startTime);

        if (!otaActive) {
            if (otaWasActive) {
                internetHealth.reset(startTime);
                mqtt.resetBackoff();
                lowHeapGuard.reset();
            }

            tickActuator(startTime);

            if (display) {
                display->update();
            }

            if (lowHeapGuard.sample(heap_caps_get_free_size(MALLOC_CAP_INTERNAL))) {
                vTaskDelay(500 / portTICK_PERIOD_MS);
                ESP.restart();
            }

            ntp.tick(startTime, internetHealth);
            mqtt.tick(startTime, bootMs, ntp.currentEpoch(), internetHealth);

            if (internetHealth.shouldForceReconnect(startTime)) {
                ESP_LOGW(TAG, "Internet connectivity lost (%u failures) - forcing WiFi reconnect",
                         internetHealth.failures());
                wifi.forceReconnect();
            }
        }

        otaWasActive = otaActive;

        if (startTime - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
            lastDiagnostics = startTime;
            logDiagnostics(startTime);
        }

        const uint32_t elapsedMs = millis() - startTime;
        if (elapsedMs > 500) {
            ESP_LOGD(TAG, "Tick slow work: work=%lums wait=%lums status=%d",
                     static_cast<unsigned long>(elapsedMs), static_cast<unsigned long>(LOOP_TICKS_MS),
                     WiFi.status());
        }

        stats.add(elapsedMs);

        lastElapsedMs = elapsedMs;
    }
#else
    for (;;) {
    }
#endif
}

void Network::startTask() {
#ifdef ARDUINO
    // Stack size is measured, not guessed. Like SensorMonitor's, this stack is
    // carved out of *internal* SRAM, the pool that OTA downloads, lwIP and
    // AsyncTCP contend for; 20480 B was reserved on a "for stability" hunch while
    // 17.3% of it was ever touched.
    //
    // Measured peak: 3544 B used (HWM reported 16936 B free of 20480) after a run
    // covering the deepest paths this task takes — station bring-up with WiFi
    // init and association, NTP sync, MQTT connect + publish, and the syslog
    // formatting buffer inside the logging macro. 8192 keeps ~2.3x headroom
    // rather than the 3x used for SensorMonitor, because two paths had not been
    // exercised when the mark was taken: the AP/captive-portal fallback and the
    // mDNS re-advertisement on reconnect. The periodic "Network task stack HWM"
    // diagnostic re-measures this; raise it if the number ever approaches 0.
    xTaskCreate(
        taskWrapper, // Task Function
        "Network", // Task Name
        8192, // Stack Size (measured peak 3544 B, ~2.3x headroom)
        this, // Parameters
        1, // Priority
        &taskHandle // Task Handle
    );
#endif
}

void Network::taskWrapper(void *pvParameters) {
    ESP_LOGI(TAG, "taskWrapper()");
    auto *instance = static_cast<Network *>(pvParameters);
    instance->task();
}

void Network::initialize_watchdog_timer() {
    esp_err_t wdtAdd = esp_task_wdt_add(NULL);
    if (wdtAdd != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_add failed (err 0x%x) - task runs unguarded", wdtAdd);
    }
}

void Network::enable_webserver() {
    ESP_LOGI(TAG, "Switching webserver to OPERATIONAL mode...");
    if (webServer) {
        webServer->setMode(WebServerMode::OPERATIONAL);
    } else {
        ESP_LOGE(TAG, "webServer not wired up — bug in main.cpp ordering");
    }
    ESP_LOGI(TAG, "Webserver started - system ready, free heap: %u bytes", ESP.getFreeHeap());
}
