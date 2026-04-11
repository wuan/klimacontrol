#ifndef KLIMACONTROL_SYSLOG_OUTPUT_H
#define KLIMACONTROL_SYSLOG_OUTPUT_H

#include "Config.h"

#ifdef ARDUINO
#include <WiFiUdp.h>
#include <freertos/semphr.h>

class SyslogOutput {
public:
    // Install the syslog vprintf handler. Chains to the previous handler
    // so UART output is preserved. Thread-safe via mutex.
    static void begin(const Config::SyslogConfig& config);

    // Uninstall the syslog handler and restore the previous one.
    static void end();

    // Update target without reinstalling the handler.
    static void setConfig(const Config::SyslogConfig& config);

    static bool isActive() { return active; }

private:
    static int syslogVprintf(const char* fmt, va_list args);

    // Map ESP log level character to syslog severity (RFC 3164).
    // E=3(error), W=4(warning), I=6(info), D=7(debug), V=7(debug)
    static int levelToSeverity(char level);

    static WiFiUDP udp;
    static SemaphoreHandle_t mutex;
    static vprintf_like_t previousHandler;
    static Config::SyslogConfig currentConfig;
    static bool active;
};

#endif // ARDUINO
#endif // KLIMACONTROL_SYSLOG_OUTPUT_H
