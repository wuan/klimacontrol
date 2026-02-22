//
// Touch Controller for ledz
// Handles capacitive touch input for loading presets
//

#ifndef LEDZ_TOUCH_CONTROLLER_H
#define LEDZ_TOUCH_CONTROLLER_H

#include "Config.h"

#include "SensorController.h"

class TouchController {
private:
    Config::ConfigManager &config;
    SensorController &sensorController;
    Config::TouchConfig touchConfig;

    // Touch pin GPIO numbers for ESP32-S3
    // Using GPIO 5, 6, 7 which are touch-capable on QT Py ESP32-S3
    static constexpr uint8_t TOUCH_PINS[Config::TouchConfig::MAX_TOUCH_PINS] = {9, 7, 5};

    // Debounce tracking
    bool wasTouched[Config::TouchConfig::MAX_TOUCH_PINS];
    uint32_t lastTouchTime[Config::TouchConfig::MAX_TOUCH_PINS];
    static constexpr uint32_t DEBOUNCE_MS = 500; // Minimum time between triggers
    
    struct ShowVariantGroup {
        const char* showName;
        const char* const* variants;
        size_t numVariants;
    };
    
    static const ShowVariantGroup SHOW_VARIANTS[];
    static const size_t NUM_SHOW_VARIANTS;
    
    int currentShowIdx = 0;
    int currentVariantIdx = 0;

public:
    /**
     * Constructor
     * @param config Configuration manager reference
     * @param showController Show controller reference for loading presets
     */
    TouchController(Config::ConfigManager &config, SensorController &sensorController);

    /**
     * Initialize the touch controller - loads config from NVS
     */
    void begin();

    /**
     * Check all touch pins and load presets if touched
     * Should be called periodically (e.g., every 50-100ms)
     */
    void update();

    /**
     * Reload configuration from NVS
     */
    void reloadConfig();

    /**
     * Get current touch configuration
     */
    const Config::TouchConfig& getTouchConfig() const { return touchConfig; }

    /**
     * Update touch configuration
     */
    void setTouchConfig(const Config::TouchConfig& newConfig);

    /**
     * Check if touch control is enabled
     */
    bool isEnabled() const { return touchConfig.enabled; }

    /**
     * Set enabled state
     */
    void setEnabled(bool enabled);

    /**
     * Set touch threshold
     */
    void setThreshold(uint16_t threshold);

    /**
     * Get current touch values for debugging
     * @param values Array to store touch values (must be MAX_TOUCH_PINS size)
     */
    void getTouchValues(uint32_t* values) const;

    /**
     * Get the GPIO pin number for a touch index
     */
    static uint8_t getGpioPin(uint8_t touchIndex) {
        if (touchIndex < Config::TouchConfig::MAX_TOUCH_PINS) {
            return TOUCH_PINS[touchIndex];
        }
        return 0;
    }
};

#endif //LEDZ_TOUCH_CONTROLLER_H
