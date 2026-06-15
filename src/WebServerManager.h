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
 * Webserver operating mode.
 *
 * The same `WebServerManager` instance is reused across mode flips
 * (AP config vs. operational STA) by calling `setMode()`. Constructing
 * a new server on every mode flip would carve a large hole in the heap
 * each time and is the fragmentation source this class is designed
 * to avoid — see spec `memory-management` → "Long-lived singletons are
 * constructed once".
 */
enum class WebServerMode {
    NONE,        // server not started
    CONFIG,      // AP-mode: only serves the WiFi configuration page + /api/wifi
    OPERATIONAL  // STA-mode: serves the full control UI and API surface
};

/**
 * WebServerManager
 *
 * Single class for both AP (configuration) and STA (operational) modes.
 * Mode is selected by `setMode()` which internally calls `clearRoutes()`
 * followed by the appropriate route-setup helper. The instance is
 * long-lived: construct once in `setup()` and switch modes by calling
 * `setMode()` instead of destroying and reconstructing the server.
 */
class WebServerManager {
protected:
    Config::ConfigManager &config;
    Network &network;
    SensorController &sensorController;
    Task::SensorMonitor &sensorMonitor;
    WebServerMode currentMode = WebServerMode::NONE;

#ifdef ARDUINO
    AsyncWebServer server;
    AccessLogger logging;
#endif

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

    // Route setup methods (implemented in routes/*.cpp)
    void setupPageRoutes();
    void setupStatusRoutes();
    void setupSensorRoutes();
    void setupControlRoutes();
    void setupSettingsRoutes();
    void setupOTARoutes();
    void setupMqttRoutes();
    void setupSyslogRoutes();
    void setupI2CRoutes();

    /**
     * Handle WiFi configuration POST request
     */
#ifdef ARDUINO
    void handleWiFiConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
#endif

    /**
     * Drop all previously-registered handlers and the not-found hook
     * so a `setMode()` call starts from a clean route table. Stops the
     * server while the table is rebuilt so a request can't see a
     * partial set of routes mid-swap.
     */
    void clearRoutes();

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

    /**
     * Switch the active route set to the given mode. Stops the server,
     * clears all previously-registered handlers, registers the routes
     * for the new mode, and re-starts the server. Calling with the
     * current mode is a no-op (returns immediately).
     *
     * @param mode One of WebServerMode::CONFIG or WebServerMode::OPERATIONAL
     */
    void setMode(WebServerMode mode);
};

#endif //KLIMACONTROL_WEBSERVERMANAGER_H