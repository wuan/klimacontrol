#include "NetworkWatchdog.h"

#ifdef ARDUINO
#include <esp_task_wdt.h>
#endif

namespace Support {

    namespace {
        WdtResetFn& hookSlot() {
            static WdtResetFn hook = nullptr;
            return hook;
        }

        WdtResetFn defaultHook() {
#ifdef ARDUINO
            return []() { esp_task_wdt_reset(); };
#else
            return []() {};
#endif
        }
    } // namespace

    void setWdtResetHook(WdtResetFn fn) {
        hookSlot() = std::move(fn);
    }

    void resetWdtResetHook() {
        hookSlot() = nullptr;
    }

    void feedWdt() {
        WdtResetFn& slot = hookSlot();
        if (!slot) {
            slot = defaultHook();
        }
        slot();
    }

} // namespace Support
