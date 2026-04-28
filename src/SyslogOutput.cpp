#include "SyslogOutput.h"

#ifdef ARDUINO
#include <WiFi.h>

// Static member definitions
WiFiUDP SyslogOutput::udp;
SemaphoreHandle_t SyslogOutput::mutex = nullptr;
Config::SyslogConfig SyslogOutput::currentConfig{};
char SyslogOutput::hostname[32] = "";
bool SyslogOutput::active = false;

void SyslogOutput::setHostname(const char* name) {
    strlcpy(hostname, name && name[0] ? name : "klima", sizeof(hostname));
}

void SyslogOutput::begin(const Config::SyslogConfig& config) {
    if (active) return;

    currentConfig = config;
    if (!config.enabled || config.host[0] == '\0') return;

    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
    }

    active = true;
}

void SyslogOutput::end() {
    if (!active) return;
    active = false;
}

void SyslogOutput::setConfig(const Config::SyslogConfig& config) {
    bool doBegin = false;
    bool doEnd = false;

    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool wasEnabled = currentConfig.enabled;
        currentConfig = config;
        doBegin = (!wasEnabled && config.enabled && !active);
        doEnd = (wasEnabled && !config.enabled && active);
        xSemaphoreGive(mutex);
    } else {
        // No mutex yet — update directly
        currentConfig = config;
        if (config.enabled && !active) {
            begin(config);
        }
        return;
    }

    // begin()/end() called after releasing the mutex so they don't
    // deadlock if they ever need to acquire it internally.
    if (doBegin) {
        begin(config);
    } else if (doEnd) {
        end();
    }
}

void SyslogOutput::send(char level, const char* tag, const char* msg) {
    if (!active || WiFi.status() != WL_CONNECTED) return;

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return; // Skip if we can't get the mutex quickly
    }

    // RFC 3164: <PRI>MSG
    // Facility 1 (user) = 8, priority = facility*8 + severity
    int severity = levelToSeverity(level);
    int pri = 8 + severity;

    // RFC 3164: <PRI>HOSTNAME TAG: MSG
    char syslogBuf[300];
    int len = snprintf(syslogBuf, sizeof(syslogBuf), "<%d>%s %s: %s", pri, hostname, tag, msg);
    if (len > (int)sizeof(syslogBuf) - 1) {
        len = sizeof(syslogBuf) - 1;
    }

    udp.beginPacket(currentConfig.host, currentConfig.port);
    udp.write(reinterpret_cast<const uint8_t*>(syslogBuf), len);
    udp.endPacket();

    xSemaphoreGive(mutex);
}

int SyslogOutput::levelToSeverity(char level) {
    switch (level) {
        case 'E': return 3; // error
        case 'W': return 4; // warning
        case 'I': return 6; // informational
        case 'D': return 7; // debug
        case 'V': return 7; // debug
        default:  return 6; // informational
    }
}

#endif // ARDUINO
