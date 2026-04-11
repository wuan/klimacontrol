#ifndef KLIMACONTROL_LOG_H
#define KLIMACONTROL_LOG_H

//
// Project-level logging macros.
//
// On Arduino (ESP32): outputs via Serial (USB CDC) with millisecond
// timestamps and level/tag prefixes. This bypasses the ets_printf path
// used by the framework's log_printf, avoiding noisy internal messages
// from libraries like Preferences, WiFi, etc.
//
// On native builds: no-ops (for unit test compilation).
//
// Usage is identical to ESP-IDF logging:
//   static const char* TAG = "net";
//   ESP_LOGI(TAG, "Connected to %s", ssid);
//

#ifdef ARDUINO

#include <Arduino.h>
#include <esp_timer.h>
#include "SyslogOutput.h"

// Undef any existing definitions from esp32-hal-log.h / esp_log.h
#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV

#define _KLIMA_LOG(letter, tag, format, ...) do { \
    Serial.printf("%08lu" #letter "%6s " format "\r\n", \
                  (unsigned long)(esp_timer_get_time() / 1000ULL), \
                  tag, ##__VA_ARGS__); \
    if (SyslogOutput::isActive()) { \
        char _syslog_buf[256]; \
        snprintf(_syslog_buf, sizeof(_syslog_buf), format, ##__VA_ARGS__); \
        SyslogOutput::send(#letter [0], tag, _syslog_buf); \
    } \
} while(0)

#define ESP_LOGE(tag, format, ...) _KLIMA_LOG(E, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) _KLIMA_LOG(W, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) _KLIMA_LOG(I, tag, format, ##__VA_ARGS__)

#ifndef CORE_DEBUG_LEVEL
#define CORE_DEBUG_LEVEL 0
#endif

#if CORE_DEBUG_LEVEL >= 4
#define ESP_LOGD(tag, format, ...) _KLIMA_LOG(D, tag, format, ##__VA_ARGS__)
#else
#define ESP_LOGD(tag, format, ...) do {} while(0)
#endif

#if CORE_DEBUG_LEVEL >= 5
#define ESP_LOGV(tag, format, ...) _KLIMA_LOG(V, tag, format, ##__VA_ARGS__)
#else
#define ESP_LOGV(tag, format, ...) do {} while(0)
#endif

// Keep esp_log_level_set available but as a no-op — our macros don't use
// the ESP-IDF runtime log system.
#define esp_log_level_set(tag, level) (void)0

#else // !ARDUINO — native build stubs

#define ESP_LOGE(tag, fmt, ...)
#define ESP_LOGW(tag, fmt, ...)
#define ESP_LOGI(tag, fmt, ...)
#define ESP_LOGD(tag, fmt, ...)
#define ESP_LOGV(tag, fmt, ...)
#define esp_log_level_set(tag, level) (void)0

#endif // ARDUINO

#endif // KLIMACONTROL_LOG_H
