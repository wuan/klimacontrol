// StatusLed - Simple status LED control using built-in NeoPixel
//

#ifndef KLIMACONTROL_STATUSLED_H
#define KLIMACONTROL_STATUSLED_H

#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#endif

/**
 * Status LED states
 */
enum class LedState {
    OFF,       // LED is off
    ON,        // LED is on
    STARTUP, // Slow blinking (1 Hz)
    TRANSMIT_DATA,  // Brief white flash during MQTT publish
    ERROR      // Solid red — fatal init error (e.g. mutex allocation failure)
};

/**
 * Status LED controller using built-in NeoPixel
 * Controls the onboard NeoPixel to indicate device status
 */
class StatusLed {
private:
#ifdef ARDUINO
    Adafruit_NeoPixel pixel;
#endif
    LedState state;
    float progress;                // 0.0 = green, 1.0 = red (MQTT interval progress)
#ifdef ARDUINO
    uint32_t lastShownColor = 0xFFFFFFFF; // init to impossible value to force first write
#endif

    void showColor(uint32_t color);

public:
    /**
     * Constructor
     * @param pin GPIO pin connected to the NeoPixel
     * @param numPixels Number of pixels (usually 1 for built-in)
     */
    StatusLed();

    /**
     * Initialize the LED
     */
    void begin();

    /**
     * Set LED state
     * @param newState New state for the LED
     */
    void setState(LedState newState);

    /**
     * Get current LED state
     * @return Current LED state
     */
    LedState getState() const { return state; }

    /**
     * Update LED state (call regularly from main loop)
     */
    void update();

    /**
     * Turn LED on
     */
    void on();

    /**
     * Turn LED off
     */
    void off();

    /**
     * Toggle LED state
     */
    void toggle();
    
    /**
     * Set MQTT progress for green→red gradient in ON state
     * @param progress 0.0 = green (just published), 1.0 = red (about to publish)
     */
    void setProgress(float progress);

    /**
     * Get current progress value
     * @return Current progress (0.0-1.0)
     */
    [[nodiscard]] float getProgress() const { return progress; }
};

#endif //KLIMACONTROL_STATUSLED_H