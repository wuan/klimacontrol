#include "SyslogOutput.h"

#ifdef ARDUINO
#include "Log.h"
#include <WiFi.h>

// Static member definitions
WiFiUDP SyslogOutput::udp;
SemaphoreHandle_t SyslogOutput::mutex = nullptr;
vprintf_like_t SyslogOutput::previousHandler = nullptr;
Config::SyslogConfig SyslogOutput::currentConfig{};
bool SyslogOutput::active = false;

static const char* TAG = "syslog";

void SyslogOutput::begin(const Config::SyslogConfig& config) {
    if (active) return;

    currentConfig = config;
    if (!config.enabled || config.host[0] == '\0') return;

    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
    }

    previousHandler = esp_log_set_vprintf(syslogVprintf);
    active = true;

    ESP_LOGI(TAG, "Syslog started -> %s:%u", config.host, config.port);
}

void SyslogOutput::end() {
    if (!active) return;

    esp_log_set_vprintf(previousHandler);
    previousHandler = nullptr;
    active = false;
}

void SyslogOutput::setConfig(const Config::SyslogConfig& config) {
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool wasEnabled = currentConfig.enabled;
        currentConfig = config;
        xSemaphoreGive(mutex);

        if (!wasEnabled && config.enabled && !active) {
            begin(config);
        } else if (wasEnabled && !config.enabled && active) {
            end();
        }
    }
}

int SyslogOutput::syslogVprintf(const char* fmt, va_list args) {
    // Always forward to UART first
    int ret = previousHandler(fmt, args);

    // Only send to syslog if WiFi is connected
    if (!currentConfig.enabled || WiFi.status() != WL_CONNECTED) {
        return ret;
    }

    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ret; // Skip if we can't get the mutex quickly
    }

    // Format the message into a buffer
    char buf[256];
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (len <= 0) {
        xSemaphoreGive(mutex);
        return ret;
    }
    if (len >= (int)sizeof(buf)) {
        len = sizeof(buf) - 1;
    }

    // Strip trailing \r\n
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    if (len == 0) {
        xSemaphoreGive(mutex);
        return ret;
    }

    // Determine syslog severity from ESP_LOG format: "X (1234) tag: msg"
    // The first character is the log level letter
    int severity = levelToSeverity(buf[0]);

    // RFC 3164 syslog: <PRI>MSG
    // Facility 1 (user) = 8, priority = facility*8 + severity
    int pri = 8 + severity;

    // Build and send the UDP packet
    char syslogBuf[300];
    int syslogLen = snprintf(syslogBuf, sizeof(syslogBuf), "<%d>%s", pri, buf);
    if (syslogLen > (int)sizeof(syslogBuf)) {
        syslogLen = sizeof(syslogBuf);
    }

    udp.beginPacket(currentConfig.host, currentConfig.port);
    udp.write(reinterpret_cast<const uint8_t*>(syslogBuf), syslogLen);
    udp.endPacket();

    xSemaphoreGive(mutex);
    return ret;
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
