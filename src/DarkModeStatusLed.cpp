#include "DarkModeStatusLed.h"

void DarkModeStatusLed::begin(uint32_t nowMs) {
    led.begin();
    onSinceMs = nowMs;
}

void DarkModeStatusLed::setState(LedState newState) {
    if (logicalState == newState) return;

    const bool wasNormal = isNormalOperation(logicalState);
    const bool isNormal = isNormalOperation(newState);

    if (isNormal && !wasNormal) {
        // Entering normal operation: start the dark-mode clock.
        darkAnchorArmed = true;
        onSinceMs = lastNowMs;
    } else if (!isNormal) {
        // OFF / STARTUP / ERROR: leave normal operation, re-arm on next entry.
        darkAnchorArmed = false;
    }
    // ON <-> TRANSMIT_DATA: anchor untouched, so the publish flash cannot
    // keep resetting the timer.

    logicalState = newState;
    applyEffectiveState(lastNowMs);
}

bool DarkModeStatusLed::isDark(uint32_t nowMs) const {
    const uint32_t threshold = darkAfterMs.load();
    if (threshold == 0 || !darkAnchorArmed) return false;
    return (nowMs - onSinceMs) >= threshold; // wrap-safe unsigned arithmetic
}

void DarkModeStatusLed::applyEffectiveState(uint32_t nowMs) {
    const bool suppress = isNormalOperation(logicalState) && isDark(nowMs);
    if (suppress) {
        if (!suppressed) {
            led.setState(LedState::OFF);
            led.setPowerRail(false); // black is latched, now cut the rail
        }
    } else {
        if (suppressed) led.setPowerRail(true);   // re-power before rendering
        led.setState(logicalState);
    }
    suppressed = suppress;
}

void DarkModeStatusLed::update(uint32_t nowMs) {
    lastNowMs = nowMs;
    applyEffectiveState(nowMs);
    led.update();
}

void DarkModeStatusLed::setDarkAfterSeconds(uint16_t seconds) {
    darkAfterMs.store(static_cast<uint32_t>(seconds) * 1000u);
}
