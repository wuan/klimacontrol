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
#include <set>
#endif

Network::Network(Config::ConfigManager &config, SensorController &sensorController)
    : config(config), sensorController(sensorController), mode(NetworkMode::NONE), webServer(nullptr), statusLed(nullptr)
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

    // Start mDNS responder
    String hostname = generateHostname();

    if (MDNS.begin(hostname.c_str())) {
        Serial.print("mDNS responder started: ");
        Serial.print(hostname);
        Serial.println(".local");

        // Load device config for custom name
        Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        bool hasCustomName = (deviceConfig.device_name[0] != '\0' &&
                              strcmp(deviceConfig.device_name, deviceConfig.device_id) != 0);

        // Set instance name with custom device name or device ID
        String instanceName;
        if (hasCustomName) {
            instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_name);
        } else {
            instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_id);
        }
        MDNS.setInstanceName(instanceName.c_str());
        Serial.print("mDNS instance name: ");
        Serial.println(instanceName);

        // Advertise HTTP service
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error starting mDNS responder!");
    }

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

    // Disable WiFi power saving for low latency
    WiFi.setSleep(WIFI_PS_NONE);

    // Set transmit power to maximum
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    Serial.println("WiFi Configuration:");
    Serial.printf("  Power save: NONE\n");
    Serial.printf("  TX Power: 19.5dBm (max)\n");

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
        Serial.println("\nWiFi Diagnostics:");
        Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
        Serial.printf("  BSSID: %s\n", WiFi.BSSIDstr().c_str());
        Serial.printf("  Channel: %d\n", WiFi.channel());
        Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
        Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
        Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
        Serial.printf("  TX Power: %d\n", WiFi.getTxPower());
        Serial.printf("  Sleep Mode: %d (0=NONE)\n", WiFi.getSleep());
        Serial.printf("  Auto Reconnect: %d\n", WiFi.getAutoReconnect());
        Serial.println();

        // Start mDNS responder
        String hostname = generateHostname();

        if (MDNS.begin(hostname.c_str())) {
            Serial.print("mDNS responder started: ");
            Serial.print(hostname);
            Serial.println(".local");

            // Load device config for custom name
            Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
            bool hasCustomName = (deviceConfig.device_name[0] != '\0' &&
                                  strcmp(deviceConfig.device_name, deviceConfig.device_id) != 0);

            // Set instance name with custom device name or device ID
            String instanceName;
            if (hasCustomName) {
                instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_name);
            } else {
                instanceName = Constants::INSTANCE_NAME_PREFIX + String(deviceConfig.device_id);
            }
            MDNS.setInstanceName(instanceName.c_str());
            Serial.print("mDNS instance name: ");
            Serial.println(instanceName);

            // Advertise HTTP service
            MDNS.addService("http", "tcp", 80);

            Serial.print("You can now access ");
            Serial.print(Constants::PROJECT_NAME);
            Serial.println(" at:");
            Serial.print("  http://");
            Serial.print(hostname);
            Serial.println(".local/");
            Serial.print("  or http://");
            Serial.println(WiFi.localIP());
        } else {
            Serial.println("Error starting mDNS responder!");
        }

        // Start NTP client
        ntpClient.begin();
        ntpClient.update();

        Serial.print("NTP time: ");
        Serial.println(ntpClient.getFormattedTime());

        // Initialize timer scheduler
        timerScheduler = std::make_unique<TimerScheduler>(config, sensorController);
        timerScheduler->begin();
        timerScheduler->setNtpAvailable(true);
    } else {
        Serial.println("\nConnection failed");
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
    statusLed = std::make_unique<StatusLed>(PIN_NEOPIXEL, 1); // Built-in NeoPixel, 1 pixel
    statusLed->begin();
    statusLed->setState(LedState::BLINK_SLOW); // Indicate booting
    
    // Initialize touch controller early (works without WiFi)
    touchController = std::make_unique<TouchController>(config, sensorController);
    touchController->begin();

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
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());

    // Main loop - NTP updates and touch control
    auto lastNtpUpdate = ntpClient.getEpochTime();

    unsigned long lastCheck = millis();
    unsigned long lastSecond = millis();
    while (true) {
        // Short delay for responsive touch control
        vTaskDelay(50 / portTICK_PERIOD_MS);

        unsigned long now = millis();

        // Update status LED frequently (every 50ms)
        if (statusLed) {
            statusLed->update();
        }
        
        // Check touch controller frequently (every 50ms)
        if (touchController) {
            touchController->update();
        }

        // Check WiFi and timers every second
        if (now - lastSecond >= 1000) {
            lastSecond = now;

            auto wl_status = WiFi.status();
            if (now - lastCheck > 1100) {
                Serial.printf("WiFi status: %d delayed %lu\n", wl_status, now - lastCheck);
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
                    Serial.printf("WiFi reconnected (%d attempts)\n", attempts);
                }
            }

            if (ntpClient.getEpochTime() - lastNtpUpdate > 600) {
                bool result = ntpClient.update();
                Serial.print("NTP update: ");
                Serial.print(ntpClient.getFormattedTime());
                Serial.print(" - ");
                Serial.println(result ? "success" : "failed");
                lastNtpUpdate = ntpClient.getEpochTime();
            }

            // Check timers
            if (timerScheduler) {
                timerScheduler->checkTimers(ntpClient.getEpochTime());
            }
        }
    }
#endif
}

void Network::startTask() {
    xTaskCreate(
        taskWrapper, // Task Function
        "Network", // Task Name
        10000, // Stack Size
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
