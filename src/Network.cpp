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
    : config(config), sensorController(sensorController), temperatureController(temperatureController),
      sensorMonitor(sensorMonitor), statusLed(statusLed),
      mdns(config), provisioning(config, mdns), wifi(config), mqtt(sensorController, statusLed),
      webServer(webServer) {
    provisioning.setWebServer(webServer);
}

void Network::begin() {
    mqtt.begin();
}

void Network::setWebServer(WebServerManager *server) {
    webServer = server;
    provisioning.setWebServer(server);
}

void Network::setDisplay(Display::DisplayManager *displayManager) {
    display = displayManager;
    provisioning.setDisplay(displayManager);
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
    // Snapshot the config: configure() reads the host and channel as a pair,
    // and a concurrent updateActuatorAssignment() on the web task could
    // otherwise leave us with the new host and the old channel (or vice
    // versa) — a corrupted assignment.
    const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();
    heatingActuator.configure(cfg);
    heatingActuator.tick(temperatureController.getControlOutput(),
                         temperatureController.isHeatingPermitted(), now);
    // Publish what the relay is actually doing, so isControlActive() and
    // everything downstream report confirmed state rather than this
    // controller's intent.
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

[[noreturn]] void Network::task() {
#ifdef ARDUINO
    const uint32_t bootMs = millis(); // baseline for boot-relative checks (wrap-safe via subtraction)

    // Subscribe to the TWDT. setup() initializes the TWDT before creating this
    // task, so this should always succeed; log loudly if it does not, because an
    // unsubscribed task makes every esp_task_wdt_reset() in the station bring-up
    // and the loop a silent no-op and removes the 30s stall protection entirely.
    esp_err_t wdtAdd = esp_task_wdt_add(NULL);
    if (wdtAdd != ESP_OK) {
        ESP_LOGE(TAG, "esp_task_wdt_add failed (err 0x%x) - task runs unguarded", wdtAdd);
    }

    ESP_LOGI(TAG, "Network task started");

    // Status LED is owned by main.cpp (top-level object); the network task
    // just drives it. begin() must be called once after construction.
    statusLed.begin(bootMs);
    // Dark-mode threshold must be in force before the first ON transition.
    statusLed.setDarkAfterSeconds(config.loadEnergyConfig().led_dark_after_s);
    statusLed.setState(LedState::STARTUP); // Indicate booting

    if (!config.isConfigured()) {
        ESP_LOGI(TAG, "No WiFi configuration found - starting AP mode");
        mode = NetworkMode::AP;
        provisioning.runFirstBoot();
    }
    ESP_LOGI(TAG, "Network task configured");

    // Fall back to AP mode every AP_FALLBACK_THRESHOLD-th failure (3, 6, 9,
    // ...) to allow user reconfiguration while still periodically retrying
    // STA mode for temporary outages. Each AP timeout increments the failure
    // counter, so the device eventually offers AP mode again.
    static constexpr uint8_t AP_FALLBACK_THRESHOLD = 3;
    const uint8_t failures = config.getConnectionFailures();
    ESP_LOGI(TAG, "Previous connection failures: %u", failures);
    if (failures > 0 && failures % AP_FALLBACK_THRESHOLD == 0) {
        mode = NetworkMode::AP;
        provisioning.runFallbackWindow(failures);
    }

    const Config::WiFiConfig wifiConfig = config.loadWiFiConfig();
    ESP_LOGI(TAG, "WiFi configured - starting STA mode");
    if (!startSTA(wifiConfig.ssid, wifiConfig.password)) {
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

    // Connection successful - reset failure counter
    config.resetConnectionFailures();

    statusLed.setState(LedState::ON); // Solid on for connected state

    // Switch the long-lived web server to OPERATIONAL mode. The same
    // WebServerManager instance from boot is reused — no re-allocation, no
    // heap fragmentation. See spec `memory-management` → "Long-lived
    // singletons are constructed once".
    ESP_LOGI(TAG, "Switching webserver to OPERATIONAL mode...");
    if (webServer) {
        webServer->setMode(WebServerMode::OPERATIONAL);
    } else {
        ESP_LOGE(TAG, "webServer not wired up — bug in main.cpp ordering");
    }
    ESP_LOGI(TAG, "Webserver started - system ready, free heap: %u bytes", ESP.getFreeHeap());

    // Set syslog hostname early so it's available if syslog is enabled later via API
    SyslogOutput::setHostname(mdns.hostname().c_str());
    SyslogOutput::begin(config.loadSyslogConfig());

    // Main loop - 1 s housekeeping: LED, actuator, MQTT, NTP, diagnostics
    uint32_t lastSecond = millis();
    // Tracks when the previous 1 s block finished its work. Used together with
    // the new entry time to attribute long iterations to either in-block work
    // (this task is slow) or external wait (another priority-1 task held the CPU).
    uint32_t lastBlockExitMs = millis();
    uint32_t lastDiagnostics = millis();
    bool otaWasActive = false;
    wifi.beginSupervision(millis());

    static constexpr uint32_t DIAGNOSTICS_INTERVAL_MS = 900000; // 15 minutes
    static constexpr uint32_t TICK_MS_FINE = 1000;
    static constexpr uint32_t WAKE_MARGIN_MS = 2;

    while (true) {
        if (lastWorkMs < TICK_MS_FINE) {
            const uint32_t sleepMs = (TICK_MS_FINE - lastWorkMs + WAKE_MARGIN_MS);
            vTaskDelay(pdMS_TO_TICKS(sleepMs));
        }

        esp_task_wdt_reset();

        const uint32_t now = millis();

        // An OTA download owns the network for minutes and squeezes internal
        // SRAM down to what the TLS session leaves over. Everything in this task
        // that either competes for that memory or interprets a failure as "the
        // link is broken" has to stand down for the duration — see the
        // individual guards below. Sampled once per iteration so all of them see
        // one consistent view.
        const bool otaActive = OTAUpdater::isUpdateInProgress();

        statusLed.update(now);

        // Heating actuator. Stood down during an OTA; the relay's own lease
        // closes the valve if the download outlasts it, which is exactly what
        // the lease is for.
        if (!otaActive) tickActuator(now);

        // Repaint the e-paper display if the refresh policy calls for it. The
        // common case returns immediately without touching SPI; an actual
        // refresh blocks ~0.5-2.6 s on the panel's BUSY line and feeds the task
        // watchdog either side (see Display::EPaperDisplay::render). Skipped
        // during OTA so nothing competes for memory or the SPI bus.
        if (display && !otaActive) {
            display->update();
        }

        if (lowHeapGuard.sample(heap_caps_get_free_size(MALLOC_CAP_INTERNAL), otaActive)) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP.restart();
        }

        if (now - lastSecond >= 1000) {
            lastSecond = now;
            [[maybe_unused]] const uint32_t waitMs = now - lastBlockExitMs;

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

            ntp.tick(now, otaActive, internetHealth);

            // MQTT is the most expensive thing this task does while the flash is
            // being written: MqttClient::loop() opens a TCP socket (a fresh lwIP
            // PCB plus a 1 KB TX buffer out of the internal pool the download
            // has already drained) and blocks up to TCP_CONNECT_TIMEOUT_MS + the
            // handshake on every retry, so the 1 s tick that also drives the
            // status LED and the WiFi supervision stalls for seconds at a time.
            // Worse, each failed attempt was counted as an internet outage and
            // eventually tore the link down under the downloader. The broker may
            // drop us on keepalive; that is fine — a successful update reboots
            // anyway, and a failed one reconnects on the next tick.
            if (!otaActive) mqtt.tick(now, bootMs, ntp.currentEpoch(), internetHealth);

            // Internet connectivity recovery: repeated failures from MQTT, OTA or
            // NTP with WiFi still "connected" mean the internet is gone, not the
            // WiFi; re-associating usually clears it.
            //
            // Never while an OTA download is running. The recovery action tears
            // the link out from under the OTA worker mid-transfer ("Connection
            // lost during download"), and because nothing resets the counter
            // without a success it would fire again every window, leaving the
            // device unreachable for as long as updates keep being attempted.
            // The premise — "repeated failures mean the internet is gone" —
            // doesn't hold during an OTA, when MQTT and NTP fail because the
            // link and the internal heap are saturated by a transfer that is
            // itself proof the internet works.
            if (otaWasActive && !otaActive) {
                // Start the post-OTA window clean: failures recorded around the
                // download describe the download, not the link.
                internetHealth.reset(now);
                mqtt.resetBackoff();
            }
            otaWasActive = otaActive;

            if (!otaActive && internetHealth.shouldForceReconnect(now)) {
                ESP_LOGW(TAG, "Internet connectivity lost (%u failures) - forcing WiFi reconnect",
                         internetHealth.failures());
                wifi.forceReconnect();
            }

            if (now - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
                lastDiagnostics = now;
                logDiagnostics(now);
            }

            // Iteration timing diagnostic at DEBUG level. We only log when this
            // task's own work is slow — large `wait` is expected RTOS scheduling
            // contention with SensorMonitor (same priority, single core) and is
            // harmless. workMs > 500 ms points at something blocking inside the
            // block (MQTT loop, NTP, mDNS) and is worth investigating.
            const uint32_t blockExit = millis();
            const uint32_t workMs = blockExit - now;
            if (workMs > 500) {
                ESP_LOGD(TAG, "Tick slow work: work=%lums wait=%lums status=%d",
                         static_cast<unsigned long>(workMs), static_cast<unsigned long>(waitMs),
                         WiFi.status());
            }

            stats.add(workMs);
            // Carry this iteration's work duration to the top of the next
            // iteration so the adaptive sleep there can subtract it from
            // TICK_MS. Same value just recorded into stats; one variable,
            // two readers (the stats accumulator across iterations and the
            // immediate next sleep).
            lastWorkMs = workMs;
            lastBlockExitMs = blockExit;
        }
    }
#else
    for (;;) {}
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
