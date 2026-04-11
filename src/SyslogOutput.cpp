#include "SyslogOutput.h"

#ifdef ARDUINO
#include <WiFi.h>

// Static member definitions
WiFiUDP SyslogOutput::udp;
SemaphoreHandle_t SyslogOutput::mutex = nullptr;
Config::SyslogConfig SyslogOutput::currentConfig{};
char SyslogOutput::hostname[32] = "";
bool SyslogOutput::active = false;

void SyslogOutput::begin(const Config::SyslogConfig& config, const char* deviceName) {
    if (active) return;

    currentConfig = config;
    if (!config.enabled || config.host[0] == '\0') return;

    strlcpy(hostname, deviceName && deviceName[0] ? deviceName : "klima", sizeof(hostname));

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
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool wasEnabled = currentConfig.enabled;
        currentConfig = config;
        xSemaphoreGive(mutex);

        if (!wasEnabled && config.enabled && !active) {
            begin(config, hostname);
        } else if (wasEnabled && !config.enabled && active) {
            end();
        }
    } else {
        // No mutex yet — just update config
        currentConfig = config;
        if (config.enabled && !active) {
            begin(config, hostname);
        }
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
