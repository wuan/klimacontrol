#include "StatusLed.h"

StatusLed::StatusLed(uint8_t pin, uint8_t numPixels) 
    : pixel(numPixels, pin, NEO_GRB + NEO_KHZ800), state(LedState::OFF), 
      lastChange(0), ledOn(false), brightness(0.0f), direction(1),
      currentColor(0x000000), progress(0.0f), mqttFlashStart(0) {
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
    lastChange = millis();
    
    switch (state) {
        case LedState::ON:
            ledOn = true;
            brightness = 1.0f;
            currentColor = 0x000F00; // Green for normal operation
            break;
        case LedState::OFF:
            ledOn = false;
            brightness = 0.0f;
            currentColor = 0x000000; // Off
            break;
        case LedState::BLINK_SLOW:
        case LedState::BLINK_FAST:
            ledOn = true;
            brightness = 1.0f;
            currentColor = 0x00000F; // Blue for AP/config mode
            break;
        case LedState::PULSE:
            ledOn = true;
            brightness = 1.0f;
            currentColor = 0x0F0000; // Red for error/warning
            break;
        case LedState::MEASURING:
            ledOn = true;
            brightness = 1.0f;
            currentColor = 0x0F0F00; // Yellow for active measurement
            break;
        case LedState::MQTT_ACTIVE:
            ledOn = true;
            brightness = 1.0f;
            mqttFlashStart = millis();
            currentColor = 0x0F0F0F; // White for MQTT transmit flash
            break;
    }
    
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
#ifdef ARDUINO
    unsigned long now = millis();

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

        case LedState::BLINK_SLOW: {
            unsigned long interval = 500; // 500ms on, 500ms off
            if (now - lastChange >= interval) {
                ledOn = !ledOn;
                lastChange = now;
            }
            showColor(ledOn ? currentColor : 0x000000);
            break;
        }

        case LedState::BLINK_FAST: {
            unsigned long interval = 250; // 250ms on, 250ms off
            if (now - lastChange >= interval) {
                ledOn = !ledOn;
                lastChange = now;
            }
            showColor(ledOn ? currentColor : 0x000000);
            break;
        }

        case LedState::PULSE: {
            unsigned long interval = 20; // Update every 20ms
            if (now - lastChange >= interval) {
                brightness += 0.05f * direction;

                if (brightness >= 1.0f) {
                    brightness = 1.0f;
                    direction = -1;
                } else if (brightness <= 0.1f) {
                    brightness = 0.1f;
                    direction = 1;
                }

                // Apply brightness to the current color
                uint8_t r = (currentColor >> 16) & 0xFF;
                uint8_t g = (currentColor >> 8) & 0xFF;
                uint8_t b = currentColor & 0xFF;

                r = static_cast<uint8_t>(r * brightness);
                g = static_cast<uint8_t>(g * brightness);
                b = static_cast<uint8_t>(b * brightness);

                showColor((r << 16) | (g << 8) | b);
                lastChange = now;
            }
            break;
        }
        case LedState::MEASURING:
            showColor(currentColor);
            break;

        case LedState::MQTT_ACTIVE:
            if (now - mqttFlashStart >= MQTT_FLASH_DURATION_MS) {
                state = LedState::ON;
                progress = 0.0f; // Reset to green after publish
                showColor((0 << 16) | (15 << 8));
            } else {
                showColor(currentColor); // White flash
            }
            break;
    }
#endif
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

void StatusLed::setMeasuring() {
    setState(LedState::MEASURING);
}

void StatusLed::setProgress(float p) {
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    progress = p;
}