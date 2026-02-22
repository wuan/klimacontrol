//
// Touch Controller implementation
//

#include "TouchController.h"
#include "ShowController.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

// Define the static member
constexpr uint8_t TouchController::TOUCH_PINS[Config::TouchConfig::MAX_TOUCH_PINS];

const char* SOLID_VARIANTS[] = {
    "{\"colors\":[[255,170,120]]}",
    "{\"colors\":[[255,255,255]]}",
    "{\"colors\":[[255,0,0]]}",
    "{\"colors\":[[255,127,0]]}",
    "{\"colors\":[[255,255,0]]}",
    "{\"colors\":[[0,255,0]]}",
    "{\"colors\":[[0,255,255]]}",
    "{\"colors\":[[0,127,255]]}",
    "{\"colors\":[[0,0,255]]}",
    "{\"colors\":[[255,0,255]]}"
};
const char* COLORRANGES_VARIANTS[] = {
    "{\"colors\":[[0,0,255],[255,255,0]]}",
    "{\"colors\":[[255,0,0],[255,255,255],[0,255,0]]}",
    "{\"colors\":[[170,21,27],[241,191,0],[170,21,27]],\"ranges\":[25,75]}"
};
const char* TWOCOLORBLEND_VARIANTS[] = {
    "{\"colors\":[[0,0,255],[255,0,0]],\"gradient\":true}",
    "{\"colors\":[[0,255,0],[255,0,0]],\"gradient\":true}",
    "{\"colors\":[[0,255,0],[0,0,255]],\"gradient\":true}",
};
const char* COLORRUN_VARIANTS[] = {"{}"};
const char* JUMP_VARIANTS[] = {"{}"};
const char* RAINBOW_VARIANTS[] = {"{}"};
const char* WAVE_VARIANTS[] = {"{}"};
const char* STARLIGHT_VARIANTS[] = {
    "{\"probability\":0.1,\"length\":0,\"fade\":250}",
    "{\"probability\":0.02,\"length\":5000,\"fade\":1000}"
};
const char* THEATERCHASE_VARIANTS[] = {
    "{\"num_steps_per_cycle\":21}",
    "{\"num_steps_per_cycle\":42}",
    "{\"num_steps_per_cycle\":84}"
};
const char* MORSECODE_VARIANTS[] = {
    "{\"message\":\"foo bar baz\"}",
    "{\"message\":\"gutes neues\"}"
};

const TouchController::ShowVariantGroup TouchController::SHOW_VARIANTS[] = {
    {"Solid", SOLID_VARIANTS, 10},
    {"Solid", COLORRANGES_VARIANTS, 3},
    {"Solid", TWOCOLORBLEND_VARIANTS, 3},
    {"ColorRun", COLORRUN_VARIANTS, 1},
    {"Jump", JUMP_VARIANTS, 1},
    {"Rainbow", RAINBOW_VARIANTS, 1},
    {"Wave", WAVE_VARIANTS, 1},
    {"Starlight", STARLIGHT_VARIANTS, 2},
    {"TheaterChase", THEATERCHASE_VARIANTS, 3},
    {"MorseCode", MORSECODE_VARIANTS, 2}
};

const size_t TouchController::NUM_SHOW_VARIANTS = sizeof(TouchController::SHOW_VARIANTS) / sizeof(TouchController::SHOW_VARIANTS[0]);

TouchController::TouchController(Config::ConfigManager &config, SensorController &sensorController)
    : config(config), sensorController(sensorController), currentShowIdx(0), currentVariantIdx(0) {
    // Initialize debounce tracking
    for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
        wasTouched[i] = false;
        lastTouchTime[i] = 0;
    }
}

void TouchController::begin() {
    touchConfig = config.loadTouchConfig();
#ifdef ARDUINO
    Serial.printf("TouchController: Configuration loaded - enabled: %d, threshold: %d", touchConfig.enabled, touchConfig.threshold);
#endif

    // For temperature controller, touch interface could control target temperature
    // Initialize with current target temperature
    float currentTarget = sensorController.getTargetTemperature();
    // Map temperature range to show indices for compatibility
    // 10°C -> index 0, 30°C -> index 20 (2°C steps)
    currentShowIdx = static_cast<uint8_t>((currentTarget - 10.0f) / 2.0f);
    currentShowIdx = std::max(0, std::min(20, static_cast<int>(currentShowIdx)));

#ifdef ARDUINO
    Serial.println("TouchController: Initializing touch pins");
    for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
        const char* action = (i == 0) ? "Switch Show" : (i == 1) ? "Switch Variant" : "Switch Layout";
        Serial.printf("  Touch pin %u (GPIO %u) -> %s\n",
                      i, TOUCH_PINS[i], action);
    }
    Serial.printf("  Enabled: %s, Threshold: %u\n",
                  touchConfig.enabled ? "yes" : "no", touchConfig.threshold);
#endif
}

void TouchController::update() {
    if (!touchConfig.enabled) {
        return;
    }

#ifdef ARDUINO
    uint32_t now = millis();

    for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
        // Read touch value
        uint32_t touchValue = touchRead(TOUCH_PINS[i]);

        // Check if touched (value below threshold means touched)
        bool isTouched = touchValue > touchConfig.threshold;

        // Detect rising edge (transition from not-touched to touched)
        if (isTouched && !wasTouched[i]) {
            // Check debounce
            if (now - lastTouchTime[i] >= DEBOUNCE_MS) {
                lastTouchTime[i] = now;

                if (i == 0) {
                    // Button 1: Increase target temperature (+2°C)
                    float currentTemp = sensorController.getTargetTemperature();
                    float newTemp = std::min(30.0f, currentTemp + 2.0f);
                    sensorController.setTargetTemperature(newTemp);
                    
                    Serial.printf("TouchController: Increased target temperature to %.1f°C\n", newTemp);
                } else if (i == 1) {
                    // Button 2: Decrease target temperature (-2°C)
                    float currentTemp = sensorController.getTargetTemperature();
                    float newTemp = std::max(10.0f, currentTemp - 2.0f);
                    sensorController.setTargetTemperature(newTemp);
                    
                    Serial.printf("TouchController: Decreased target temperature to %.1f°C\n", newTemp);
                } else if (i == 2) {
                    // Button 3: Toggle temperature control
                    bool currentlyEnabled = sensorController.isControlEnabled();
                    sensorController.setControlEnabled(!currentlyEnabled);
                    
                    Serial.printf("TouchController: Temperature control %s\n",
                                  currentlyEnabled ? "disabled" : "enabled");
                }
            }
        }

        wasTouched[i] = isTouched;
    }
#endif
}

void TouchController::reloadConfig() {
    touchConfig = config.loadTouchConfig();
#ifdef ARDUINO
    Serial.println("TouchController: Configuration reloaded");
#endif
}

void TouchController::setTouchConfig(const Config::TouchConfig& newConfig) {
    touchConfig = newConfig;
    config.saveTouchConfig(touchConfig);
}

void TouchController::setEnabled(bool enabled) {
    touchConfig.enabled = enabled;
    config.saveTouchConfig(touchConfig);
#ifdef ARDUINO
    Serial.printf("TouchController: %s\n", enabled ? "Enabled" : "Disabled");
#endif
}

void TouchController::setThreshold(uint16_t threshold) {
    touchConfig.threshold = threshold;
    config.saveTouchConfig(touchConfig);
#ifdef ARDUINO
    Serial.printf("TouchController: Threshold set to %u\n", threshold);
#endif
}

void TouchController::getTouchValues(uint32_t* values) const {
#ifdef ARDUINO
    for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
        values[i] = touchRead(TOUCH_PINS[i]);
    }
#else
    for (uint8_t i = 0; i < Config::TouchConfig::MAX_TOUCH_PINS; i++) {
        values[i] = 0;
    }
#endif
}
