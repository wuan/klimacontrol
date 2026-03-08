// WebServer Manager - handles HTTP server and API endpoints
//

#ifndef KLIMACONTROL_WEBSERVERMANAGER_H
#define KLIMACONTROL_WEBSERVERMANAGER_H

#ifdef ARDUINO
#include <ESPAsyncWebServer.h>
#include <Arduino.h>
#endif

// Forward declarations
namespace Config {
    class ConfigManager;
}

namespace Task {
    class SensorMonitor;
}

class Network;
class SensorController;

class AccessLogger : public AsyncMiddleware {
public:
    void run(__unused AsyncWebServerRequest* request, __unused ArMiddlewareNext next) override;
};

/**
 * Base WebServer Manager
 * Abstract base class for different webserver modes
 */
class WebServerManager {
protected:
#ifdef ARDUINO
    AsyncWebServer server;
    AccessLogger logging;
#endif

    Config::ConfigManager &config;
    Network &network;
    SensorController &sensorController;
    Task::SensorMonitor &sensorMonitor;

    /**
     * Setup shared routes for assets (CSS, favicon, etc.)
     */
    void setupCommonRoutes();

    /**
     * Setup WiFi configuration routes
     */
    void setupConfigRoutes();

    /**
     * Setup API routes for show control
     */
    void setupAPIRoutes();

    /**
     * Handle WiFi configuration POST request
     */
#ifdef ARDUINO
    void handleWiFiConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
#endif

    /**
     * Setup routes - implemented by subclasses
     */
    virtual void setupRoutes() = 0;

public:
    /**
     * WebServerManager constructor
     * @param config Configuration manager reference
     * @param network Network manager reference
     * @param sensorController SensorController reference
     */
    WebServerManager(Config::ConfigManager &config, Network &network, SensorController &sensorController, Task::SensorMonitor &sensorMonitor);

    /**
     * Virtual destructor
     */
    virtual ~WebServerManager() = default;

    /**
     * Start the webserver
     * Calls setupRoutes() then starts server
     */
    void begin();

    /**
     * Stop the webserver
     */
    void end();
};

/**
 * Config WebServer Manager
 * Webserver for AP mode - only serves WiFi configuration pages
 */
class ConfigWebServerManager : public WebServerManager {
protected:
    void setupRoutes() override;

public:
    ConfigWebServerManager(Config::ConfigManager &config, Network &network, SensorController &sensorController, Task::SensorMonitor &sensorMonitor);
};

/**
 * Operational WebServer Manager
 * Webserver for STA mode - serves full LED control interface
 */
class OperationalWebServerManager : public WebServerManager {
protected:
    void setupRoutes() override;

public:
    OperationalWebServerManager(Config::ConfigManager &config, Network &network, SensorController &sensorController, Task::SensorMonitor &sensorMonitor);
};

#endif //KLIMACONTROL_WEBSERVERMANAGER_H