// DarkModeStatusLed - decorator around StatusLed that adds a time-based
// dark mode without touching the underlying state machine.
//

#ifndef KLIMACONTROL_DARKMODESTATUSLED_H
#define KLIMACONTROL_DARKMODESTATUSLED_H

#include <atomic>
#include <cstdint>

#include "StatusLed.h"

/**
 * Wraps a StatusLed and decides, per update, what state to forward to it.
 *
 * Callers use this class exactly like StatusLed. It tracks the *logical*
 * state (what the firmware asked for) and forwards an *effective* state to
 * the wrapped LED: identical to the logical state, except that ON and
 * TRANSMIT_DATA become OFF once the device has been in normal operation for
 * `darkAfterMs`. STARTUP and ERROR always pass through.
 *
 * "Normal operation" is the ON state including its TRANSMIT_DATA flashes.
 * The dark-mode clock is anchored when ON is entered from any other state
 * and cleared on OFF/STARTUP/ERROR, so a reconnect re-lights the LED for a
 * fresh period while the 15 s publish flash cannot keep resetting it.
 *
 * While suppressing, the wrapper also cuts the NeoPixel supply rail via
 * StatusLed::setPowerRail(false); a dark WS2812 still draws quiescent
 * current. The rail is restored on the release edge, before the effective
 * state is forwarded, so the re-powered pixel is re-rendered in the same
 * call. Only dark mode touches the rail: a logical OFF leaves it on.
 *
 * Time is supplied by the caller (`update(nowMs)`), never read from millis()
 * here, so the class is fully testable on the native build. `setState()`
 * applies immediately using the last clock value seen by `update()`.
 */
class DarkModeStatusLed {
private:
    StatusLed led;
    LedState logicalState = LedState::OFF;
    uint32_t lastNowMs = 0;        // most recent clock passed to update()

    // Written from the HTTP handler task, read from the network task.
    std::atomic<uint32_t> darkAfterMs{0};
    // Only touched from the network task (plus the one-off init-time error path).
    bool darkAnchorArmed = false;
    uint32_t onSinceMs = 0;
    bool suppressed = false;       // dark mode currently holds the LED dark (rail is cut)

    void applyEffectiveState(uint32_t nowMs);
    [[nodiscard]] static bool isNormalOperation(LedState s) {
        return s == LedState::ON || s == LedState::TRANSMIT_DATA;
    }

public:
    DarkModeStatusLed() = default;

    /** Initialize the wrapped LED. */
    void begin(uint32_t nowMs, uint16_t led_dark_after_s);

    /**
     * Set the logical LED state. Applied immediately using the last clock
     * seen by update().
     */
    void setState(LedState newState);

    /** @return The logical state (what was requested), even while dark. */
    [[nodiscard]] LedState getState() const { return logicalState; }

    /**
     * Re-evaluate dark mode and re-render (call once per second).
     * @param nowMs Monotonic millisecond clock (millis() on the firmware)
     */
    void update(uint32_t nowMs);

    /**
     * Configure dark mode. Safe to call from any task.
     * @param seconds Seconds of normal operation before the LED goes dark; 0 disables
     */
    void setDarkAfterSeconds(uint16_t seconds);

    /** @return Configured dark-mode threshold in seconds (0 = disabled) */
    [[nodiscard]] uint16_t getDarkAfterSeconds() const {
        return static_cast<uint16_t>(darkAfterMs.load() / 1000u);
    }

    /**
     * @param nowMs Clock to evaluate against
     * @return true if dark mode currently suppresses ON/TRANSMIT_DATA
     */
    [[nodiscard]] bool isDark(uint32_t nowMs) const;

    /** Forwarded to the wrapped LED. */
    void setProgress(float progress) { led.setProgress(progress); }
    [[nodiscard]] float getProgress() const { return led.getProgress(); }

    /** Convenience forwards, mirroring StatusLed. */
    void on() { setState(LedState::ON); }
    void off() { setState(LedState::OFF); }
    void toggle() { setState(logicalState == LedState::ON ? LedState::OFF : LedState::ON); }

    /** The wrapped LED, for diagnostics and tests (effective state, colour). */
    [[nodiscard]] const StatusLed &inner() const { return led; }
};

#endif //KLIMACONTROL_DARKMODESTATUSLED_H
