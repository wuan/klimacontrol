#ifndef KLIMACONTROL_HARDWARE_WATCHDOG_H
#define KLIMACONTROL_HARDWARE_WATCHDOG_H

// Independent hardware watchdog backstop (RTC_WDT).
//
// The Task Watchdog Timer (TWDT, configured in main.cpp) catches a single task
// that stops feeding it, but it depends on the FreeRTOS scheduler and timer-group
// interrupts still being alive. The RTC watchdog lives in the RTC power domain
// and resets the chip independently of the scheduler — it is the backstop for a
// total hang (interrupts disabled, scheduler wedged) that the TWDT cannot catch.
//
// Fed from loop() (the Arduino loopTask). The timeout is deliberately longer than
// the 30s TWDT so the TWDT fires first under an ordinary task stall, and this
// only triggers when the system has hung so hard the TWDT itself can't run.

#ifdef ARDUINO

#include <esp_idf_version.h>
#include "Log.h"

// soc/rtc_wdt.h exposes the RTC_WDT register helpers on IDF 4.x (the
// arduino-esp32 2.0.x toolchain this project targets). The header moved in
// IDF 5.x; fall back to a no-op there (TWDT still applies) rather than breaking
// the build, mirroring the version guard around esp_task_wdt_init in main.cpp.
#if ESP_IDF_VERSION_MAJOR < 5
#include "soc/rtc_wdt.h"
#define KLIMA_HW_WDT_AVAILABLE 1
#endif

namespace HardwareWatchdog {

    inline const char *TAG() { return "hwwdt"; }

    // Enable the RTC watchdog with the given timeout (ms). Call once, after the
    // tasks that keep loop() fed are running.
    inline void begin(uint32_t timeoutMs) {
#ifdef KLIMA_HW_WDT_AVAILABLE
        rtc_wdt_protect_off();
        rtc_wdt_disable();
        rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
        rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
        rtc_wdt_set_time(RTC_WDT_STAGE0, timeoutMs);
        rtc_wdt_enable();
        rtc_wdt_protect_on();
        ESP_LOGI(TAG(), "RTC hardware watchdog enabled (%u ms)", timeoutMs);
#else
        (void) timeoutMs;
        ESP_LOGW(TAG(), "RTC hardware watchdog unavailable on IDF >= 5; "
                        "relying on task watchdog only");
#endif
    }

    // Pet the watchdog. A no-op when begin() was a no-op, so it is always safe
    // to call from the loop.
    inline void feed() {
#ifdef KLIMA_HW_WDT_AVAILABLE
        rtc_wdt_protect_off();
        rtc_wdt_feed();
        rtc_wdt_protect_on();
#endif
    }

} // namespace HardwareWatchdog

#endif // ARDUINO

#endif // KLIMACONTROL_HARDWARE_WATCHDOG_H
