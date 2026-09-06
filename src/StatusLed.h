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
    STARTUP, // Steady dim blue while booting / associating
    TRANSMIT_DATA,  // Brief white flash during MQTT publish
    ERROR      // Solid red — fatal init error (e.g. mutex allocation failure)
};

/**
 * Status LED controller using built-in NeoPixel
 * Controls the onboard NeoPixel to indicate device status.
 *
 * This class is a pure state-to-colour mapping with no notion of time.
 * Time-based policies (e.g. dark mode) live in wrappers such as
 * DarkModeStatusLed, which decide what state to forward here.
 */
class StatusLed {
private:
#ifdef ARDUINO
    Adafruit_NeoPixel pixel;
#endif
    LedState state;
    float progress;                // 0.0 = green, 1.0 = red (MQTT interval progress)
    uint32_t lastShownColor = 0xFFFFFFFF; // init to impossible value to force first write

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
     * Set LED state (applied immediately)
     * @param newState New state for the LED
     */
    void setState(LedState newState);

    /**
     * Get current LED state
     * @return Current LED state
     */
    LedState getState() const { return state; }

    /**
     * Re-render the current state (call regularly from the network task)
     */
    void update();

    /**
     * @return Colour most recently pushed to the pixel (0xRRGGBB). For tests
     *         and diagnostics; 0xFFFFFFFF before the first write.
     */
    [[nodiscard]] uint32_t lastColor() const { return lastShownColor; }

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
