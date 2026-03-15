//
// CpuMonitor – estimates CPU load using the ESP-IDF FreeRTOS idle hook.
//
// The idle hook is called each time the FreeRTOS idle task completes one
// iteration.  With light sleep enabled (the default in this project), the
// idle task sleeps for exactly one FreeRTOS tick between hook calls, so:
//
//   cpu_load % = (1 – idle_ticks * portTICK_PERIOD_MS / elapsed_ms) × 100
//
// NOTE: Accuracy depends on light sleep being active.  If light sleep is
// disabled or fails to configure, the idle hook may be called many more
// times per tick and the reported load will appear higher than actual.
// In that case the value is still directionally correct (more idle hook
// calls → lower load) but the absolute percentage will be off.
//
// On non-ARDUINO builds (native tests) getLoad() always returns 0.
//

#ifndef KLIMACONTROL_CPUMONITOR_H
#define KLIMACONTROL_CPUMONITOR_H

#include <atomic>
#include <cstdint>

class CpuMonitor {
public:
    /** Return the singleton instance. */
    static CpuMonitor& instance();

    /**
     * Register the idle hook.  Call once from setup() before tasks start.
     * Safe to call multiple times (subsequent calls are no-ops).
     */
    void begin();

    /**
     * Return the estimated CPU load in percent (0–100).
     * The value is computed from idle-hook invocations since the previous
     * call; the very first call after begin() may return 0 until enough
     * time has passed.
     */
    float getLoad();

private:
    CpuMonitor() = default;

#ifdef ARDUINO
    static bool idleHook();

    std::atomic<uint32_t> idleCount{0};
    uint32_t lastIdleCount = 0;
    uint32_t lastTimeMs    = 0;
#endif

    float lastLoad = 0.0f;
    bool  started  = false;
};

#endif // KLIMACONTROL_CPUMONITOR_H
