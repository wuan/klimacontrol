// CaptivePortal - DNS server for captive portal functionality
// Redirects all DNS queries to the device IP in AP mode
//

#ifndef LEDZ_CAPTIVEPORTAL_H
#define LEDZ_CAPTIVEPORTAL_H

#ifdef ARDUINO
#include <DNSServer.h>
#include <WiFi.h>
#endif

/**
 * CaptivePortal
 * Simple DNS server wrapper that redirects all queries to the AP IP
 * This causes phones/tablets to automatically open the config page
 */
class CaptivePortal {
private:
#ifdef ARDUINO
    DNSServer dnsServer;
#endif
    bool running = false;

public:
    CaptivePortal();

    /**
     * Start the captive portal
     * Starts DNS server on port 53, redirecting all queries to AP IP
     */
    void begin();

    /**
     * Process DNS requests
     * Must be called regularly from main loop or task
     */
    void handleClient();

    /**
     * Stop the captive portal
     */
    void end();

    /**
     * Check if captive portal is running
     */
    bool isRunning() const { return running; }
};

#endif //LEDZ_CAPTIVEPORTAL_H
