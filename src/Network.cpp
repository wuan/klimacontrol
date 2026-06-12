#include <sstream>

#include "Constants.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#endif

#include "Network.h"
#include "Config.h"
#include "DeviceId.h"
#include "OTAUpdater.h"
#include "WebServerManager.h"
#include "SyslogOutput.h"

#ifdef ARDUINO
#include <esp_pm.h>
#include <esp_task_wdt.h>
#include "Log.h"
#endif

#ifdef ARDUINO
#include <set>
#endif

static const char* TAG = "net";

#ifdef ARDUINO
namespace {
    const char* wifiDisconnectReasonStr(uint8_t reason) {
        switch (reason) {
            case WIFI_REASON_UNSPECIFIED:           return "UNSPECIFIED";
            case WIFI_REASON_AUTH_EXPIRE:           return "AUTH_EXPIRE";
            case WIFI_REASON_AUTH_LEAVE:            return "AUTH_LEAVE";
            case WIFI_REASON_ASSOC_EXPIRE:          return "ASSOC_EXPIRE";
            case WIFI_REASON_ASSOC_TOOMANY:         return "ASSOC_TOOMANY";
            case WIFI_REASON_NOT_AUTHED:            return "NOT_AUTHED";
            case WIFI_REASON_NOT_ASSOCED:           return "NOT_ASSOCED";
            case WIFI_REASON_ASSOC_LEAVE:           return "ASSOC_LEAVE";
            case WIFI_REASON_ASSOC_NOT_AUTHED:      return "ASSOC_NOT_AUTHED";
            case WIFI_REASON_BEACON_TIMEOUT:        return "BEACON_TIMEOUT";
            case WIFI_REASON_NO_AP_FOUND:           return "NO_AP_FOUND";
            case WIFI_REASON_AUTH_FAIL:             return "AUTH_FAIL";
            case WIFI_REASON_ASSOC_FAIL:            return "ASSOC_FAIL";
            case WIFI_REASON_HANDSHAKE_TIMEOUT:     return "HANDSHAKE_TIMEOUT";
            case WIFI_REASON_CONNECTION_FAIL:       return "CONNECTION_FAIL";
            default:                                return "OTHER";
        }
    }
}
#endif

Network::Network(Config::ConfigManager &config, SensorController &sensorController, Task::SensorMonitor &sensorMonitor)
    : config(config), sensorController(sensorController), sensorMonitor(sensorMonitor), mode(NetworkMode::NONE)
#ifdef ARDUINO
      , ntpClient(wifiUdp)
#endif
      , webServer(nullptr), statusLed(nullptr), lastMqttPublish(0)
{
}

String Network::generateHostname() {
    if (cachedHostname.isEmpty()) {
#ifdef ARDUINO
        String deviceId = DeviceId::getDeviceId();
        cachedHostname = Constants::HOSTNAME_PREFIX + deviceId;
        cachedHostname.toLowerCase();
#else
        cachedHostname = Constants::PROJECT_NAME;
#endif
    }
    return cachedHostname;
}

void Network::configureMDNS() {
#ifdef ARDUINO
    String hostname = generateHostname();

    if (MDNS.begin(hostname.c_str())) {
        ESP_LOGI(TAG, "mDNS responder started: %s.local", hostname.c_str());

        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        bool deviceNameNotEmpty = deviceConfig.device_name[0] != '\0';
        bool deviceNameIsNotDeviceId = strcmp(deviceConfig.device_name, deviceConfig.device_id) != 0;
        bool hasCustomName = (deviceNameNotEmpty && deviceNameIsNotDeviceId);

        if (hasCustomName) {
            mdnsInstanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_name);
        } else {
            mdnsInstanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_id);
        }

        ESP_LOGI(TAG, "mDNS instance name: '%s'", mdnsInstanceName.c_str());
        MDNS.setInstanceName(mdnsInstanceName.c_str());

        MDNS.addService("http", "tcp", 80);
    } else {
        ESP_LOGE(TAG, "Error starting mDNS responder");
    }
#endif
}

void Network::setStatusLedState(LedState state) {
    if (statusLed) {
        statusLed->setState(state);
    }
}

LedState Network::getStatusLedState() const {
    if (statusLed) {
        return statusLed->getState();
    }
    return LedState::OFF;
}

void Network::startAP() {
#ifdef ARDUINO
    mode = NetworkMode::AP;

    // Get device ID for AP SSID
    String ap_ssid = Constants::AP_SSID_PREFIX + DeviceId::getDeviceId();

    ESP_LOGI(TAG, "Starting Access Point: %s", ap_ssid.c_str());

    // Start open AP (no password)
    WiFi.softAP(ap_ssid.c_str());

    IPAddress ip_address = WiFi.softAPIP();
    ESP_LOGI(TAG, "AP IP address: %s", ip_address.toString().c_str());

    configureMDNS();

    // Start captive portal (redirects all DNS to this device)
    captivePortal.begin();

#endif
}

void Network::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
#ifdef ARDUINO
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            lastWifiConnectMs = millis();
            ESP_LOGI(TAG, "WiFi event: STA_CONNECTED ch=%u", info.wifi_sta_connected.channel);
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            ESP_LOGI(TAG, "WiFi event: GOT_IP %s rssi=%d",
                     IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(),
                     WiFi.RSSI());
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            ESP_LOGW(TAG, "WiFi event: LOST_IP");
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            lastWifiDisconnectReason = reason;
            lastWifiDisconnectMs = millis();
            ESP_LOGW(TAG, "WiFi event: DISCONNECTED reason=%u (%s)",
                     reason, wifiDisconnectReasonStr(reason));
            break;
        }
        default:
            break;
    }
#endif
}

void Network::startSTA(const char *ssid, const char *password) {
#ifdef ARDUINO
    mode = NetworkMode::STA;

    // Clear any previous WiFi state without sending a disconnect frame.
    // Calling disconnect(true) sends a deauth frame to the AP, which can cause
    // AP-side rate limiting or blocklisting when connection attempts fail
    // repeatedly (e.g., during the restart loop bug).
    WiFi.disconnect(false);
    vTaskDelay(50 / portTICK_PERIOD_MS);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    // Register WiFi event handler for diagnostic logging and reconnect tracking.
    // Registered once; handler captures `this` for member access.
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        this->onWiFiEvent(event, info);
    });

    // Apply WiFi energy config (TX power and sleep mode)
    Config::EnergyConfig energyConfig = config.loadEnergyConfig();
    WiFi.setTxPower(static_cast<wifi_power_t>(energyConfig.wifi_power));

    // Apply WiFi sleep mode: 0=WIFI_PS_NONE, 1=WIFI_PS_MIN_MODEM, 2=WIFI_PS_MAX_MODEM
    wifi_ps_type_t sleepMode = WIFI_PS_NONE;
    const char* sleepModeStr = "NONE";
    if (energyConfig.wifi_sleep_mode == 1) {
        sleepMode = WIFI_PS_MIN_MODEM;
        sleepModeStr = "MIN_MODEM";
    } else if (energyConfig.wifi_sleep_mode == 2) {
        sleepMode = WIFI_PS_MAX_MODEM;
        sleepModeStr = "MAX_MODEM";
    }
    WiFi.setSleep(sleepMode);

    ESP_LOGI(TAG, "WiFi config: TX Power=%d, Sleep Mode=%s", WiFi.getTxPower(), sleepModeStr);

    // Try up to MAX_CONNECT_TRIES times before giving up. Each attempt waits
    // MAX_WAIT_SLOTS * 500ms (~15s) for association. Between attempts we briefly
    // back off so the AP isn't hammered.
    constexpr int MAX_CONNECT_TRIES = 3;
    constexpr int MAX_WAIT_SLOTS = 30;
    constexpr int BACKOFF_MS = 3000;

    for (int tryNum = 1; tryNum <= MAX_CONNECT_TRIES; tryNum++) {
        ESP_LOGI(TAG, "Connecting to WiFi %s (attempt %d/%d) ...", ssid, tryNum, MAX_CONNECT_TRIES);
        WiFi.begin(ssid, password);

        int slots = 0;
        while (WiFi.status() != WL_CONNECTED && slots < MAX_WAIT_SLOTS) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            esp_task_wdt_reset();
            slots++;
            if (slots % 5 == 0) {
                ESP_LOGI(TAG, "Still connecting... (%d/%d, status=%d)",
                         slots, MAX_WAIT_SLOTS, WiFi.status());
            }
        }

        if (WiFi.status() == WL_CONNECTED) break;

        ESP_LOGW(TAG, "Connect attempt %d failed (last reason=%u %s), backing off %d ms",
                 tryNum, lastWifiDisconnectReason,
                 wifiDisconnectReasonStr(lastWifiDisconnectReason), BACKOFF_MS);
        WiFi.disconnect(false);
        vTaskDelay(BACKOFF_MS / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Seed lastWifiConnectMs so a later silent drop (no DISCONNECTED event
        // delivered) still has a valid "connected since" baseline to reason from.
        if (lastWifiConnectMs == 0) lastWifiConnectMs = millis();
        ESP_LOGI(TAG, "WiFi connected, IP: %s", WiFi.localIP().toString().c_str());
        ESP_LOGD(TAG, "WiFi diagnostics: SSID=%s BSSID=%s Ch=%d RSSI=%d dBm MAC=%s",
                 WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), WiFi.channel(),
                 WiFi.RSSI(), WiFi.macAddress().c_str());
        ESP_LOGD(TAG, "WiFi network: GW=%s DNS=%s TxPwr=%d Sleep=%d AutoReconn=%d",
                 WiFi.gatewayIP().toString().c_str(), WiFi.dnsIP().toString().c_str(),
                 WiFi.getTxPower(), WiFi.getSleep(), WiFi.getAutoReconnect());

        ESP_LOGI(TAG, "Configuring mDNS...");
        configureMDNS();

        String hostname = generateHostname();
        ESP_LOGI(TAG, "%s available at http://%s.local/ or http://%s",
                 Constants::PROJECT_NAME, hostname.c_str(), WiFi.localIP().toString().c_str());

        // Start NTP client. Use forceUpdate() so we get a definite sync signal —
        // update() short-circuits and returns true if its internal interval hasn't elapsed,
        // which would not actually exchange any packets.
        ESP_LOGI(TAG, "Starting NTP...");
        ntpClient.begin();
        if (ntpClient.forceUpdate()) {
            ntpSynced = true;
            lastNtpUpdateEpoch = ntpClient.getEpochTime();
            ESP_LOGI(TAG, "NTP time: %s", ntpClient.getFormattedTime().c_str());
        } else {
            ESP_LOGW(TAG, "NTP initial sync failed; will retry");
        }

        // Initialize MQTT client
        ESP_LOGI(TAG, "Initializing MQTT...");
        mqttClient = std::make_unique<MqttClient>();
        Config::MqttConfig mqttConfig = config.loadMqttConfig();
        mqttClient->begin(mqttConfig);
        ESP_LOGI(TAG, "MQTT initialized");
    } else {
        ESP_LOGE(TAG, "WiFi connection failed");
    }
#endif
}

void Network::configureUsingAPMode() {
    // Start Access Point mode
    startAP();

    // Create and start config webserver for AP mode
    webServer = std::make_unique<ConfigWebServerManager>(config, *this, sensorController, sensorMonitor);
    webServer->begin();

    // Wait for configuration
    while (!config.isConfigured()) {
        captivePortal.handleClient(); // Handle DNS requests
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Configuration received");

    // Reset failure counter since user has provided new credentials
    config.resetConnectionFailures();

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Stopping captive portal");
    captivePortal.end();

    ESP_LOGI(TAG, "Scheduling restart");
    config.requestRestart(1000);

    // Stay in loop until main loop restarts us
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

[[noreturn]] void Network::task() {
#ifdef ARDUINO
    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "Network task started");

    // Initialize status LED early (works without WiFi) - uses built-in NeoPixel
    statusLed = std::make_unique<StatusLed>(); // Built-in NeoPixel, 1 pixel
    statusLed->begin();
    statusLed->setState(LedState::STARTUP); // Indicate booting
    
    if (!config.isConfigured()) {
        ESP_LOGI(TAG, "No WiFi configuration found - starting AP mode");
        
        configureUsingAPMode();
    }
    ESP_LOGI(TAG, "Network task configured");

    // Check connection failure count — fall back to AP mode after repeated failures
    static constexpr uint8_t AP_FALLBACK_THRESHOLD = 3;
    uint8_t failures = config.getConnectionFailures();
    ESP_LOGI(TAG, "Previous connection failures: %u", failures);

    // Enter AP mode every 3rd failure (3, 6, 9, ...) to allow user reconfiguration
    // while still periodically retrying STA mode for temporary outages
    if (failures > 0 && failures % AP_FALLBACK_THRESHOLD == 0) {
        // Open AP mode for WiFi reconfiguration.
        // Device will enter AP mode periodically (every 3 failures) to allow
        // user reconfiguration, while still retrying STA mode in between.
        // Each AP timeout increments the failure counter, ensuring device
        // will eventually enter AP mode again for reconfiguration.

        static constexpr unsigned long AP_FALLBACK_TIMEOUT_MS = 5UL * 60 * 1000; // 5 minutes
        ESP_LOGW(TAG, "Multiple connection failures (%u) - opening AP for %lu s for reconfiguration",
                 failures, AP_FALLBACK_TIMEOUT_MS / 1000);

        startAP();
        webServer = std::make_unique<ConfigWebServerManager>(config, *this, sensorController, sensorMonitor);
        webServer->begin();

        unsigned long apStart = millis();
        while (!config.isConfigured() && (millis() - apStart < AP_FALLBACK_TIMEOUT_MS)) {
            esp_task_wdt_reset();
            captivePortal.handleClient();
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        captivePortal.end();
        webServer.reset();

        if (config.isConfigured()) {
            ESP_LOGI(TAG, "New configuration received - resetting failure count and restarting...");
            config.resetConnectionFailures();
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP.restart();
        }

        // AP fallback timed out - increment counter so device will enter AP mode again
        // on next boot, giving user another opportunity to reconfigure.
        // incrementConnectionFailures() already writes wifi_failures to NVS.
        uint8_t newFailures = config.incrementConnectionFailures();
        ESP_LOGW(TAG, "AP fallback timed out (total failures: %u) - restarting to retry AP mode...",
                 newFailures);

        // Brief pause so the restart isn't instantaneous
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP.restart();
    }

    // Load WiFi configuration
    Config::WiFiConfig wifiConfig = config.loadWiFiConfig();

    ESP_LOGI(TAG, "WiFi configured - starting STA mode");

    // Start Station mode
    ESP_LOGI(TAG, "Calling startSTA...");
    startSTA(wifiConfig.ssid, wifiConfig.password);
    ESP_LOGI(TAG, "startSTA finished");

    // Check if connection succeeded
    if (WiFi.status() != WL_CONNECTED) {
        // incrementConnectionFailures() already persists wifi_failures to NVS.
        uint8_t newFailures = config.incrementConnectionFailures();
        ESP_LOGW(TAG, "Failed to connect (failure %u/%u) - waiting before retry...",
                 newFailures, AP_FALLBACK_THRESHOLD);

        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP.restart();
    }

    // Connection successful - reset failure counter
    config.resetConnectionFailures();
    
    if (statusLed) {
        statusLed->setState(LedState::ON); // Solid on for connected state
    }

    // Create and start operational webserver for STA mode
    ESP_LOGI(TAG, "Creating OperationalWebServerManager...");
    webServer = std::make_unique<OperationalWebServerManager>(config, *this, sensorController, sensorMonitor);
    ESP_LOGI(TAG, "Starting webserver...");
    webServer->begin();
    ESP_LOGI(TAG, "Webserver started");

    ESP_LOGI(TAG, "Webserver started - system ready, free heap: %u bytes", ESP.getFreeHeap());

    // Set syslog hostname early so it's available if syslog is enabled later via API
    String syslogHostname = generateHostname();
    SyslogOutput::setHostname(syslogHostname.c_str());

    // Start syslog forwarding if configured
    Config::SyslogConfig syslogConfig = config.loadSyslogConfig();
    SyslogOutput::begin(syslogConfig);

    // Main loop - NTP updates and touch control
    const unsigned long bootMs = millis(); // baseline for boot-relative checks (wrap-safe via subtraction)
    unsigned long lastSecond = millis();
    // Tracks when the previous 1 s block finished its work. Used together with
    // the new entry time to attribute long iterations to either in-block work
    // (this task is slow) or external wait (another priority-1 task held the CPU).
    unsigned long lastBlockExitMs = millis();
    unsigned long lastDiagnostics = millis();
    unsigned long lastNtpRetry = 0; // millis() of last NTP retry when unsynced
    bool wasConnected = true;  // Track WiFi state transitions for mDNS re-advertisement

    // Flapping-immune restart backstop. The active-reconnect path below resets
    // activeReconnectFailures to 0 on *any* brief reconnect, so a link that
    // flickers connected-then-dropped repeatedly never trips the attempt-based
    // restart and the device appears stuck offline. Track when WiFi last held a
    // *stable* connection (continuously up for >= STABLE_CONNECT_MS); a momentary
    // flicker does not advance it. If no stable connection occurs for
    // FORCE_RESTART_NO_STABLE_MS, force a clean restart. We enter this loop
    // connected, so both baselines start at "now".
    unsigned long connectedSinceMs = millis();   // start of the current connected streak (0 = down)
    unsigned long lastStableConnectMs = millis(); // last time the link was confirmed stable
    static constexpr uint32_t MIN_FREE_HEAP_BYTES = 16384; // 16 KB
    static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 300000; // 5 minutes
    static constexpr unsigned long NTP_UNSYNCED_RETRY_MS = 60000; // 1 minute
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        esp_task_wdt_reset();

        unsigned long now = millis();

        if (statusLed) {
            statusLed->update();
        }

        // if (touchController) {
        //     touchController->update();
        // }

        // Low heap check - restart cleanly before an OOM crash
        // Skip during OTA: TLS buffers temporarily consume most internal SRAM
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < MIN_FREE_HEAP_BYTES && !OTAUpdater::isUpdateInProgress()) {
            ESP_LOGE(TAG, "CRITICAL: Low heap %u bytes - restarting...", freeHeap);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP.restart();
        }

        if (now - lastSecond >= 1000) {
            lastSecond = now;

            // WiFi state tracking — auto-reconnect is handled by WiFi.setAutoReconnect(true)
            bool isConnected = WiFi.status() == WL_CONNECTED;
            [[maybe_unused]] unsigned long waitMs = now - lastBlockExitMs;

            if (!wasConnected && isConnected) {
                ESP_LOGI(TAG, "WiFi reconnected (IP: %s)", WiFi.localIP().toString().c_str());
                configureMDNS();
                // Reset MQTT publish timer to avoid burst after reconnection
                lastMqttPublish = now;
                activeReconnectFailures = 0;
                // Clear disconnect timestamp so the next drop is timed from when it happens,
                // not from the previous disconnect period.
                lastWifiDisconnectMs = 0;
                lastWifiConnectMs = now;
            } else if (wasConnected && !isConnected) {
                // Defensive: stamp the disconnect time from the polling path if the
                // WiFi event handler did not. Some failure modes observed in the
                // field never deliver ARDUINO_EVENT_WIFI_STA_DISCONNECTED, leaving
                // lastWifiDisconnectMs at 0 — the active-reconnect block below
                // would then never fire and the device gets stuck at WL_IDLE_STATUS.
                if (lastWifiDisconnectMs == 0) {
                    lastWifiDisconnectMs = now;
                }
                ESP_LOGW(TAG, "WiFi disconnected (last reason=%u %s) - waiting for auto-reconnect",
                         lastWifiDisconnectReason,
                         wifiDisconnectReasonStr(lastWifiDisconnectReason));
            }
            wasConnected = isConnected;

            // Active reconnect path. Arduino's setAutoReconnect(true) gives up silently on
            // some disconnect reasons (BEACON_TIMEOUT, ASSOC_EXPIRE, AUTH_EXPIRE after long
            // sessions). If we've been disconnected for ACTIVE_RECONNECT_AFTER_MS without
            // recovering, force a reconnect ourselves with exponential-ish backoff.
            // After MAX_ACTIVE_RECONNECT_FAILURES tries, restart the device — usually it's
            // a deep stack state that only a clean boot can recover from.
            static constexpr unsigned long ACTIVE_RECONNECT_AFTER_MS = 30000;
            static constexpr unsigned long ACTIVE_RECONNECT_MIN_INTERVAL_MS = 30000;
            static constexpr uint8_t MAX_ACTIVE_RECONNECT_FAILURES = 6;
            if (!isConnected) {
                unsigned long downForMs = (lastWifiDisconnectMs != 0)
                    ? (now - lastWifiDisconnectMs) : 0;
                bool dueToActiveReconnect = (now - lastActiveReconnectMs) >= ACTIVE_RECONNECT_MIN_INTERVAL_MS;
                if (downForMs >= ACTIVE_RECONNECT_AFTER_MS && dueToActiveReconnect) {
                    activeReconnectFailures++;
                    ESP_LOGW(TAG, "WiFi down %lus - forcing reconnect (attempt %u/%u, last reason=%u %s)",
                             downForMs / 1000, activeReconnectFailures,
                             MAX_ACTIVE_RECONNECT_FAILURES,
                             lastWifiDisconnectReason,
                             wifiDisconnectReasonStr(lastWifiDisconnectReason));
                    lastActiveReconnectMs = now;
                    WiFi.disconnect(false);
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    WiFi.reconnect();

                    if (activeReconnectFailures >= MAX_ACTIVE_RECONNECT_FAILURES) {
                        ESP_LOGE(TAG, "Active reconnect exhausted - restarting");
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                        ESP.restart();
                    }
                }
            }

            // Flapping-immune restart backstop (see declarations above). A
            // connection only counts as "stable" after it has held continuously
            // for STABLE_CONNECT_MS; brief flickers reset the streak and never
            // advance lastStableConnectMs. If WiFi has not been stable for
            // FORCE_RESTART_NO_STABLE_MS, only a clean boot tends to recover it.
            static constexpr unsigned long STABLE_CONNECT_MS = 60000;            // 1 min up = "stable"
            static constexpr unsigned long FORCE_RESTART_NO_STABLE_MS = 600000;  // 10 min without stability
            if (isConnected) {
                if (connectedSinceMs == 0) connectedSinceMs = now; // streak started
                if (now - connectedSinceMs >= STABLE_CONNECT_MS) {
                    lastStableConnectMs = now;
                }
            } else {
                connectedSinceMs = 0; // streak broken
            }
            if (now - lastStableConnectMs >= FORCE_RESTART_NO_STABLE_MS) {
                ESP_LOGE(TAG, "No stable WiFi for %lus (flapping or stuck) - restarting",
                         (now - lastStableConnectMs) / 1000);
                vTaskDelay(500 / portTICK_PERIOD_MS);
                ESP.restart();
            }

            // NTP update — drives off the explicit ntpSynced flag, not getEpochTime() > 0.
            static constexpr uint32_t NTP_UPDATE_INTERVAL_S = 3600;

            if (ntpSynced) {
                uint32_t currentEpoch = ntpClient.getEpochTime();
                if (currentEpoch - lastNtpUpdateEpoch >= NTP_UPDATE_INTERVAL_S) {
                    if (ntpClient.forceUpdate()) {
                        lastNtpUpdateEpoch = ntpClient.getEpochTime();
                        ESP_LOGI(TAG, "NTP update: %s", ntpClient.getFormattedTime().c_str());
                    } else {
                        ESP_LOGW(TAG, "NTP update failed");
                        // Stay synced — the previous epoch is still usable, just stale.
                    }
                }
            } else if (now - lastNtpRetry >= NTP_UNSYNCED_RETRY_MS) {
                // NTP not yet synced - retry at most once per minute
                lastNtpRetry = now;
                if (ntpClient.forceUpdate()) {
                    ntpSynced = true;
                    lastNtpUpdateEpoch = ntpClient.getEpochTime();
                    ESP_LOGI(TAG, "NTP initial sync: %s", ntpClient.getFormattedTime().c_str());
                }
            }

            // MQTT: reconnect/keepalive + publish measurements
            if (mqttClient) {
                mqttClient->loop();

                if (mqttClient->isConnected() && sensorController.isDataValid()) {
                    uint32_t intervalMs = mqttClient->getIntervalMs();

                    // Seed lastMqttPublish on first eligible cycle
                    if (lastMqttPublish == 0) lastMqttPublish = now;

                    if (intervalMs > 0 && (now - bootMs >= 60000) && (now - lastMqttPublish >= intervalMs)) {
                        // Atomic snapshot: validity and data are read under the same lock,
                        // so we never publish stale measurements after a fresh read failed.
                        auto measurements = sensorController.getValidMeasurements();
                        if (!measurements.empty()) {
                            if (statusLed) statusLed->setState(LedState::TRANSMIT_DATA);
                            lastMqttPublish = now;
                            publishMeasurements(measurements);
                        }
                    }

                    // Update MQTT progress for green→red gradient on status LED
                    if (statusLed && intervalMs > 0) {
                        float prog = static_cast<float>(now - lastMqttPublish) / intervalMs;
                        if (prog > 1.0f) prog = 1.0f;
                        statusLed->setProgress(prog);
                        statusLed->setState(LedState::ON);
                    }
                } else if (statusLed) {
                    statusLed->setProgress(0.0f);
                }
            }

            // Periodic diagnostics: heap and task stack high-water marks
            if (now - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
                lastDiagnostics = now;
                ESP_LOGD(TAG, "Diagnostics: heap=%u bytes (min=%u), uptime=%lu s",
                         ESP.getFreeHeap(), ESP.getMinFreeHeap(), now / 1000);
                if (taskHandle) {
                    ESP_LOGD(TAG, "Network task stack HWM: %u bytes",
                             uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t));
                }
            }

            // Iteration timing diagnostic at DEBUG level. We only log when this
            // task's own work is slow — large `wait` is expected RTOS scheduling
            // contention with SensorMonitor (same priority, single core) and is
            // harmless. workMs > 500 ms points at something blocking inside the
            // block (MQTT loop, NTP, mDNS) and is worth investigating.
            unsigned long blockExit = millis();
            unsigned long workMs = blockExit - now;
            if (workMs > 500) {
                ESP_LOGD(TAG, "Tick slow work: work=%lums wait=%lums status=%d",
                         workMs, waitMs, WiFi.status());
            }
            lastBlockExitMs = blockExit;
        }
    }
#endif
}

void Network::publishMeasurements(const std::vector<Sensor::Measurement>& measurements) {
#ifdef ARDUINO
    if (!mqttClient || !mqttClient->isConnected()) return;

    uint32_t epoch = getCurrentEpoch();
    const char* prefix = mqttClient->getPrefix();

    uint32_t succeeded = 0;
    uint32_t failed = 0;

    for (const auto& m : measurements) {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/%s", prefix, Sensor::measurementTypeLabel(m.type));

        char payload[256];
        if (auto* i = std::get_if<int32_t>(&m.value)) {
            snprintf(payload, sizeof(payload),
                "{\"time\":%u,\"value\":%d,\"unit\":\"%s\",\"sensor\":\"%s\",\"calculated\":%s}",
                epoch, *i, Sensor::measurementTypeUnit(m.type), m.sensor, m.calculated ? "true" : "false");
        } else {
            snprintf(payload, sizeof(payload),
                "{\"time\":%u,\"value\":%.2f,\"unit\":\"%s\",\"sensor\":\"%s\",\"calculated\":%s}",
                epoch, std::get<float>(m.value), Sensor::measurementTypeUnit(m.type), m.sensor, m.calculated ? "true" : "false");
        }

        if (mqttClient->publish(topic, payload)) {
            succeeded++;
        } else {
            failed++;
        }
    }

    mqttClient->recordPublishResult(succeeded, failed);

    if (failed > 0) {
        ESP_LOGW(TAG, "MQTT: Published %u/%u measurements (%u failed)",
                 succeeded, succeeded + failed, failed);
    }
#endif
}

void Network::updateMqttConfig(const Config::MqttConfig& mqttConfig) {
    if (mqttClient) {
        mqttClient->setConfig(mqttConfig);
    }
}

void Network::startTask() {
    xTaskCreate(
        taskWrapper, // Task Function
        "Network", // Task Name
        20480, // Stack Size (increased from 18000 for stability)
        this, // Parameters
        1, // Priority
        &taskHandle // Task Handle
    );
}

void Network::taskWrapper(void *pvParameters) {
    ESP_LOGI(TAG, "taskWrapper()");
    auto *instance = static_cast<Network *>(pvParameters);
    instance->task();
}
