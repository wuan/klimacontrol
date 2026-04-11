#ifndef KLIMACONTROL_SYSLOG_OUTPUT_H
#define KLIMACONTROL_SYSLOG_OUTPUT_H

#include "Config.h"

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <freertos/semphr.h>

class SyslogOutput {
public:
    static void begin(const Config::SyslogConfig& config, const char* deviceName);
    static void end();
    static void setConfig(const Config::SyslogConfig& config);
    static bool isActive() { return active; }

    // Send a log line to the syslog server. Called from the _KLIMA_LOG macro.
    // level: single character (E/W/I/D/V)
    static void send(char level, const char* tag, const char* msg);

private:
    static int levelToSeverity(char level);

    static WiFiUDP udp;
    static SemaphoreHandle_t mutex;
    static Config::SyslogConfig currentConfig;
    static char hostname[32];
    static bool active;
};

#endif // ARDUINO
#endif // KLIMACONTROL_SYSLOG_OUTPUT_H
