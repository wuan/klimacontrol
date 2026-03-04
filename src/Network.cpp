#include <sstream>

#include "Constants.h"

#ifdef ARDUINO
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#endif

#include "Network.h"
#include "Config.h"
#include "DeviceId.h"
#include "WebServerManager.h"

#ifdef ARDUINO
#include <esp_pm.h>
#endif

#ifdef ARDUINO
#include <set>
#endif

Network::Network(Config::ConfigManager &config, SensorController &sensorController)
    : config(config), sensorController(sensorController), mode(NetworkMode::NONE), webServer(nullptr), statusLed(nullptr), lastMqttPublish(0)
#ifdef ARDUINO
      , ntpClient(wifiUdp)
#endif
{
}

String Network::generateHostname() {
#ifdef ARDUINO
    String deviceId = DeviceId::getDeviceId();
    String hostname = Constants::HOSTNAME_PREFIX + deviceId;
    hostname.toLowerCase();
    return hostname;
#else
    return Constants::PROJECT_NAME;
#endif
}

void Network::configureMDNS() {
#ifdef ARDUINO
    String hostname = generateHostname();

    if (MDNS.begin(hostname.c_str())) {
        Serial.print("mDNS responder started: ");
        Serial.print(hostname);
        Serial.println(".local");

        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        bool deviceNameNotEmpty = deviceConfig.device_name[0] != '\0';
        bool deviceNameIsNotDeviceId = strcmp(deviceConfig.device_name, deviceConfig.device_id) != 0;
        bool hasCustomName = (deviceNameNotEmpty && deviceNameIsNotDeviceId);

        String instanceName;
        if (hasCustomName) {
            instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_name);
        } else {
            instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_id);
        }

        Serial.printf("mDNS instance name: '%s'\r\n", instanceName.c_str());
        MDNS.setInstanceName(instanceName.c_str());

        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error starting mDNS responder!");
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

    Serial.print("Starting Access Point: ");
    Serial.println(ap_ssid.c_str());

    // Start open AP (no password)
    WiFi.softAP(ap_ssid.c_str());

    IPAddress ip_address = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(ip_address);

    configureMDNS();

    // Start captive portal (redirects all DNS to this device)
    captivePortal.begin();

#endif
}

void Network::startSTA(const char *ssid, const char *password) {
#ifdef ARDUINO
    mode = NetworkMode::STA;

    // Disconnect any previous connection
    WiFi.disconnect(true);
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // Set WiFi mode explicitly
    WiFi.mode(WIFI_STA);

    // Disable WiFi power save for best reception (matches CircuitPython default)
    WiFi.setSleep(WIFI_PS_NONE);

    Serial.println("WiFi Configuration:");
    Serial.printf("  Power save: NONE\r\n");

    WiFi.begin(ssid, password);

    Serial.print("Connecting to WiFi ...");
    Serial.println(ssid);

    // Wait for connection with timeout
    int attempts = 0;
    const int maxAttempts = 30; // 15 seconds

    while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected");

        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());

        // Print detailed WiFi diagnostics
        Serial.println("\r\nWiFi Diagnostics:");
        Serial.printf("  SSID: %s\r\n", WiFi.SSID().c_str());
        Serial.printf("  BSSID: %s\r\n", WiFi.BSSIDstr().c_str());
        Serial.printf("  Channel: %d\r\n", WiFi.channel());
        Serial.printf("  RSSI: %d dBm\r\n", WiFi.RSSI());
        Serial.printf("  MAC: %s\r\n", WiFi.macAddress().c_str());
        Serial.printf("  Gateway: %s\r\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  DNS: %s\r\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("  TX Power: %d\r\n", WiFi.getTxPower());
        Serial.printf("  Sleep Mode: %d (0=NONE)\r\n", WiFi.getSleep());
        Serial.printf("  Auto Reconnect: %d\r\n", WiFi.getAutoReconnect());
        Serial.println();

        configureMDNS();

        String hostname = generateHostname();
        Serial.print("You can now access ");
        Serial.print(Constants::PROJECT_NAME);
        Serial.println(" at:");
        Serial.print("  http://");
        Serial.print(hostname);
        Serial.println(".local/");
        Serial.print("  or http://");
        Serial.println(WiFi.localIP());

        // Start NTP client
        ntpClient.begin();
        ntpClient.update();

        Serial.print("NTP time: ");
        Serial.println(ntpClient.getFormattedTime());

        // Initialize MQTT client
        mqttClient = std::make_unique<MqttClient>();
        Config::MqttConfig mqttConfig = config.loadMqttConfig();
        mqttClient->begin(mqttConfig);
    } else {
        Serial.println("\r\nConnection failed");
    }
#endif
}

void Network::configureUsingAPMode() {
    // Start Access Point mode
    startAP();

    // Create and start config webserver for AP mode
    webServer = std::make_unique<ConfigWebServerManager>(config, *this, sensorController);
    webServer->begin();

    // Wait for configuration
    while (!config.isConfigured()) {
        captivePortal.handleClient(); // Handle DNS requests
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    Serial.println("Configuration received");

    vTaskDelay(5000 / portTICK_PERIOD_MS);

    Serial.println("Stopping captive portal");
    captivePortal.end();

    Serial.println("Scheduling restart");
    config.requestRestart(1000);

    // Stay in loop until main loop restarts us
    while (true) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

[[noreturn]] void Network::task() {
#ifdef ARDUINO
    Serial.println("Network task started");

    // Initialize status LED early (works without WiFi) - uses built-in NeoPixel
    statusLed = std::make_unique<StatusLed>(); // Built-in NeoPixel, 1 pixel
    statusLed->begin();
    statusLed->setState(LedState::BLINK_SLOW); // Indicate booting
    
    // Initialize touch controller early (works without WiFi)
    // touchController = std::make_unique<TouchController>(config, sensorController);
    // touchController->begin();

    // Check if WiFi is configured
    if (!config.isConfigured()) {
        Serial.println("No WiFi configuration found - starting AP mode");
        
        if (statusLed) {
            statusLed->setState(LedState::BLINK_FAST); // Fast blink for AP mode
        }

        configureUsingAPMode();
    }
    Serial.println("Network task configured");

    // Check connection failure count
    uint8_t failures = config.getConnectionFailures();
    Serial.print("Previous connection failures: ");
    Serial.println(failures);

    if (failures >= 3) {
        Serial.println("Too many connection failures - starting AP mode for reconfiguration");
        
        if (statusLed) {
            statusLed->setState(LedState::BLINK_FAST); // Fast blink for AP mode
        }
        
        config.markUnconfigured();
        config.resetConnectionFailures();
        configureUsingAPMode();
    }

    // Load WiFi configuration
    Config::WiFiConfig wifiConfig = config.loadWiFiConfig();

    Serial.println("WiFi configured - starting STA mode");

    // Start Station mode
    startSTA(wifiConfig.ssid, wifiConfig.password);

    // Check if connection succeeded
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Failed to connect - incrementing failure counter");
        uint8_t newFailures = config.incrementConnectionFailures();
        Serial.print("New failure count: ");
        Serial.println(newFailures);

        Serial.println("Restarting...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP.restart();
    }

    // Connection successful - reset failure counter
    config.resetConnectionFailures();
    
    if (statusLed) {
        statusLed->setState(LedState::ON); // Solid on for connected state
    }

    // Create and start operational webserver for STA mode
    webServer = std::make_unique<OperationalWebServerManager>(config, *this, sensorController);
    webServer->begin();

    Serial.println("Webserver started - system ready");
    Serial.printf("Free heap: %u bytes\r\n", ESP.getFreeHeap());

    // Main loop - NTP updates and touch control
    auto lastNtpUpdate = ntpClient.getEpochTime();

    unsigned long lastCheck = millis();
    unsigned long lastSecond = millis();
    unsigned long lastDiagnostics = millis();
    unsigned long lastNtpRetry = 0; // millis() of last NTP retry when unsynced
    uint8_t wifiReconnectFailures = 0;
    static constexpr uint8_t MAX_WIFI_RECONNECT_FAILURES = 10;
    static constexpr uint32_t MIN_FREE_HEAP_BYTES = 16384; // 16 KB
    static constexpr unsigned long DIAGNOSTICS_INTERVAL_MS = 300000; // 5 minutes
    static constexpr unsigned long NTP_UNSYNCED_RETRY_MS = 60000; // 1 minute
    while (true) {
        vTaskDelay(500 / portTICK_PERIOD_MS);

        unsigned long now = millis();

        if (statusLed) {
            statusLed->update();
        }

        // if (touchController) {
        //     touchController->update();
        // }

        // Low heap check - restart cleanly before an OOM crash
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < MIN_FREE_HEAP_BYTES) {
            Serial.printf("CRITICAL: Low heap %u bytes - restarting...\r\n", freeHeap);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            ESP.restart();
        }

        if (now - lastSecond >= 1000) {
            lastSecond = now;

            auto wl_status = WiFi.status();
            if (now - lastCheck > 1100) {
                Serial.printf("WiFi status: %d delayed %lu\r\n", wl_status, now - lastCheck);
            }
            lastCheck = now;

            // Check WiFi connection
            if (wl_status != WL_CONNECTED) {
                Serial.println("WiFi disconnected - reconnecting ...");
                WiFi.reconnect();

                int attempts = 0;
                while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                    vTaskDelay(500 / portTICK_PERIOD_MS);
                    attempts++;
                }

                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("WiFi reconnected (%d attempts)\r\n", attempts);
                    wifiReconnectFailures = 0;
                } else {
                    wifiReconnectFailures++;
                    Serial.printf("WiFi reconnect failed (%u/%u)\r\n",
                                  wifiReconnectFailures, MAX_WIFI_RECONNECT_FAILURES);
                    if (wifiReconnectFailures >= MAX_WIFI_RECONNECT_FAILURES) {
                        Serial.println("Too many WiFi reconnect failures - restarting...");
                        vTaskDelay(500 / portTICK_PERIOD_MS);
                        ESP.restart();
                    }
                }
            } else {
                wifiReconnectFailures = 0;
            }

            // NTP update - guard against epoch 0 causing uint32_t underflow
            uint32_t currentEpoch = ntpClient.getEpochTime();
            if (currentEpoch > 0 && lastNtpUpdate > 0
                    && currentEpoch - lastNtpUpdate > 3600) {
                bool result = ntpClient.update();
                Serial.print("NTP update: ");
                Serial.print(ntpClient.getFormattedTime());
                Serial.print(" - ");
                Serial.println(result ? "success" : "failed");
                if (result) {
                    lastNtpUpdate = ntpClient.getEpochTime();
                }
            } else if (currentEpoch == 0 && lastNtpUpdate == 0
                       && now - lastNtpRetry >= NTP_UNSYNCED_RETRY_MS) {
                // NTP not yet synced - retry at most once per minute
                lastNtpRetry = now;
                if (ntpClient.update()) {
                    lastNtpUpdate = ntpClient.getEpochTime();
                }
            }

            // MQTT: reconnect/keepalive + publish measurements
            if (mqttClient) {
                mqttClient->loop();

                if (mqttClient->isConnected() && sensorController.isDataValid()) {
                    uint32_t intervalMs = mqttClient->getIntervalMs();

                    // Seed lastMqttPublish on first eligible cycle
                    if (lastMqttPublish == 0) lastMqttPublish = now;

                    if (intervalMs > 0 && now >= 60000 && (now - lastMqttPublish >= intervalMs)) {
                        lastMqttPublish = now;
                        publishMeasurements(sensorController.getMeasurements());
                        if (statusLed) statusLed->setState(LedState::MQTT_ACTIVE);
                    }

                    // Update MQTT progress for green→red gradient on status LED
                    if (statusLed && intervalMs > 0) {
                        float prog = static_cast<float>(now - lastMqttPublish) / intervalMs;
                        if (prog > 1.0f) prog = 1.0f;
                        statusLed->setProgress(prog);
                    }
                } else if (statusLed) {
                    statusLed->setProgress(0.0f);
                }
            }

            // Periodic diagnostics: heap and task stack high-water marks
            if (now - lastDiagnostics >= DIAGNOSTICS_INTERVAL_MS) {
                lastDiagnostics = now;
                Serial.printf("Diagnostics: heap=%u bytes (min=%u), uptime=%lu s\r\n",
                              ESP.getFreeHeap(), ESP.getMinFreeHeap(), now / 1000);
                if (taskHandle) {
                    Serial.printf("  Network task stack HWM: %u bytes\r\n",
                                  uxTaskGetStackHighWaterMark(taskHandle) * sizeof(StackType_t));
                }
            }
        }
    }
#endif
}

void Network::publishMeasurements(const std::vector<Sensor::Measurement>& measurements) {
#ifdef ARDUINO
    if (!mqttClient || !mqttClient->isConnected()) return;

    uint32_t epoch = getCurrentEpoch();
    Config::MqttConfig mqttConfig = config.loadMqttConfig();

    for (const auto& m : measurements) {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/%s", mqttConfig.prefix, Sensor::measurementTypeLabel(m.type));

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

        mqttClient->publish(topic, payload);
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
        14000, // Stack Size
        this, // Parameters
        1, // Priority
        &taskHandle // Task Handle
    );
}

void Network::taskWrapper(void *pvParameters) {
    Serial.println("Network: taskWrapper()");
    auto *instance = static_cast<Network *>(pvParameters);
    instance->task();
}
