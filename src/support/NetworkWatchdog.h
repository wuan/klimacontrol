#ifndef KLIMACONTROL_NETWORK_WATCHDOG_H
#define KLIMACONTROL_NETWORK_WATCHDOG_H

#include <functional>

namespace Support {

    using WdtResetFn = std::function<void()>;

    /**
     * Install a custom watchdog-reset hook. The hook is called by guardedCall()
     * before and after the wrapped callable runs. Tests use this to count
     * invocations; the production code path leaves the default in place.
     */
    void setWdtResetHook(WdtResetFn fn);

    /**
     * Restore the default watchdog-reset hook. The default under ARDUINO is
     * `esp_task_wdt_reset()`; on native it is a no-op. Always call this from
     * the test `tearDown` so hook state does not leak between suites.
     */
    void resetWdtResetHook();

    /**
     * Feed the watchdog exactly once via the currently installed hook. Exposed
     * for cases where the caller wants to feed the watchdog without the
     * full before/after wrap (e.g. inside a longer hand-rolled loop).
     */
    void feedWdt();

    /**
     * Run a callable that returns a bool, feeding the FreeRTOS task watchdog
     * immediately before and after the call. Used to wrap external blocking
     * network operations in the Network task so a hung call cannot starve
     * the 30 s TWDT and force a panic reset.
     *
     * The callable is invoked exactly once. The hook is invoked exactly
     * twice: before, and after the callable returns. If the callable throws
     * the hook is still called once after the throw propagates, so the
     * watchdog is fed even in the error path.
     */
    template <typename Fn>
    bool guardedCall(Fn&& fn) {
        feedWdt();
        bool result;
        try {
            result = static_cast<bool>(fn());
        } catch (...) {
            feedWdt();
            throw;
        }
        feedWdt();
        return result;
    }

} // namespace Support

#endif // KLIMACONTROL_NETWORK_WATCHDOG_H
