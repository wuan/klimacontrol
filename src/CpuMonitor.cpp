#include "CpuMonitor.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <esp_freertos_hooks.h>
#endif

CpuMonitor& CpuMonitor::instance() {
    static CpuMonitor monitor;
    return monitor;
}

void CpuMonitor::begin() {
    if (started) {
        return;
    }
    started = true;
#ifdef ARDUINO
    lastTimeMs    = millis();
    lastIdleCount = 0;
    esp_register_freertos_idle_hook(idleHook);
#endif
}

float CpuMonitor::getLoad() {
#ifdef ARDUINO
    const uint32_t nowMs          = millis();
    const uint32_t currentIdle    = idleCount.load(std::memory_order_relaxed);
    const uint32_t elapsedMs      = nowMs - lastTimeMs;
    const uint32_t idleDelta      = currentIdle - lastIdleCount;

    lastTimeMs    = nowMs;
    lastIdleCount = currentIdle;

    if (elapsedMs == 0) {
        return lastLoad;
    }

    // Each idle-hook invocation represents approximately one FreeRTOS tick of
    // idle time (portTICK_PERIOD_MS ms) when light sleep is active.  If light
    // sleep is disabled the hook is called more frequently and the result will
    // be clamped, so the value is still directionally correct.
    const float idleMs = static_cast<float>(idleDelta) *
                         static_cast<float>(portTICK_PERIOD_MS);
    float load = (1.0f - idleMs / static_cast<float>(elapsedMs)) * 100.0f;

    if (load < 0.0f)   load = 0.0f;
    if (load > 100.0f) load = 100.0f;

    lastLoad = load;
    return load;
#else
    return 0.0f;
#endif
}

#ifdef ARDUINO
bool CpuMonitor::idleHook() {
    instance().idleCount.fetch_add(1, std::memory_order_relaxed);
    return false; // allow normal idle processing (e.g. light sleep)
}
#endif
