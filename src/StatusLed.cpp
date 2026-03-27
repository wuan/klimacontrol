#include "StatusLed.h"

StatusLed::StatusLed() 
#ifdef ARDUINO
    : pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800), state(LedState::OFF),
#else
    : state(LedState::OFF),
#endif
      progress(0.0f) {
}

void StatusLed::begin() {
#ifdef ARDUINO
    pixel.begin();
    pixel.show(); // Initialize with LED off
#endif
}

void StatusLed::setState(LedState newState) {
    if (state == newState) return;
    
    state = newState;

    update(); // Apply immediately
}

void StatusLed::showColor(uint32_t color) {
#ifdef ARDUINO
    if (color == lastShownColor) return;
    lastShownColor = color;
    pixel.setPixelColor(0, color);
    pixel.show();
#endif
}

void StatusLed::update() {
    switch (state) {
        case LedState::OFF:
            showColor(0x000000);
            break;

        case LedState::ON: {
            // Green→Red gradient based on MQTT progress
            uint8_t r = static_cast<uint8_t>(15 * progress);
            uint8_t g = static_cast<uint8_t>(15 * (1.0f - progress));
            showColor((r << 16) | (g << 8));
            break;
        }

        case LedState::STARTUP: {
            showColor(0x00000f);
            break;
        }

        case LedState::TRANSMIT_DATA:
            showColor(0x020202);
            break;
    }
}

void StatusLed::on() {
    setState(LedState::ON);
}

void StatusLed::off() {
    setState(LedState::OFF);
}

void StatusLed::toggle() {
    if (state == LedState::ON) {
        setState(LedState::OFF);
    } else {
        setState(LedState::ON);
    }
}

void StatusLed::setProgress(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    progress = p;
}