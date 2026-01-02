// ShowController - Thread-safe show management using FreeRTOS queue
//

#ifndef LEDZ_SHOWCONTROLLER_H
#define LEDZ_SHOWCONTROLLER_H

#include <memory>
#include <atomic>
#include <mutex>
#include <string>

#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#endif

#include "show/Show.h"
#include "ShowFactory.h"
#include "Config.h"
#include "strip/Base.h"
#include "strip/Strip.h"
#include "strip/Layout.h"

/**
 * Show command types for queue communication
 */
enum class ShowCommandType {
    SET_SHOW, // Change current show
    SET_BRIGHTNESS, // Change brightness
    SET_LAYOUT, // Change strip layout
    LOAD_PRESET // Load a preset (show + params + layout)
};

/**
 * Show command structure for queue
 */
struct ShowCommand {
    ShowCommandType type;
    char *show_name;
    char *params_json; // JSON parameters for show
    uint8_t brightness_value;
    bool layout_reverse;
    bool layout_mirror;
    int16_t layout_dead_leds;
};

/**
 * Show statistics for monitoring performance
 */
struct ShowStats {
    uint32_t avg_execution_time = 0; // ms
    uint32_t avg_show_time = 0;      // ms
    uint32_t avg_cycle_time = 0;     // ms
    uint32_t last_execution_time = 0; // ms
    uint32_t last_show_time = 0;      // ms
};

/**
 * ShowController
 * Thread-safe controller for managing LED shows across cores
 * - Core 1 (webserver) queues commands
 * - Core 0 (LED task) processes commands
 */
class ShowController {
private:
#ifdef ARDUINO
    QueueHandle_t commandQueue;
#endif

    ShowFactory &factory;
    Config::ConfigManager &config;

    std::unique_ptr<Show::Show> currentShow;
    std::string currentShowName;
    std::atomic<uint8_t> brightness;

    // base strip and strip layout
    std::unique_ptr<Strip::Strip> baseStrip;
    std::unique_ptr<Strip::Strip> layout;

    ShowStats stats;
    mutable std::mutex stateMutex;

    /**
     * Apply a command (called from LED task)
     */
    void applyCommand(const ShowCommand &cmd);

public:
    /**
     * ShowController constructor
     * @param factory Show factory for creating shows
     * @param config Configuration manager for persistence
     */
    ShowController(ShowFactory &factory, Config::ConfigManager &config);

    // disable copy constructor
    ShowController(const ShowController &) = delete;

    /**
     * Initialize the controller (call from setup)
     */
    void begin();

    /**
     * Queue a show change command (called from Core 1 - webserver)
     * @param showName Name of show to switch to
     * @param paramsJson JSON parameters (optional, defaults to "{}")
     * @return true if queued successfully
     */
    bool queueShowChange(const std::string &showName, const std::string &paramsJson = "{}");

    /**
     * Queue a brightness change command (called from Core 1 - webserver)
     * @param brightness New brightness value (0-255)
     * @return true if queued successfully
     */
    bool queueBrightnessChange(uint8_t brightness);

    /**
     * Queue layout change command (called from Core 1 - webserver)
     * @param reverse Reverse LED order
     * @param mirror Mirror LED pattern
     * @param dead_leds Number of dead LEDs at the end
     * @return true if queued successfully
     */
    bool queueLayoutChange(bool reverse, bool mirror, int16_t dead_leds);

    /**
     * Queue preset load command (called from Core 1 - webserver)
     * @param preset Preset to load
     * @return true if queued successfully
     */
    bool queuePresetLoad(const Config::Preset &preset);

    /**
     * Set layout and base strip pointers for runtime reconfiguration
     * @param base Pointer to the base strip
     */
    void setStrip(std::unique_ptr<Strip::Strip> &&base);

    /**
     * Process pending commands from queue (called from Core 0 - LED task)
     * Non-blocking - processes all pending commands
     */
    void processCommands();

    /**
     * Get current brightness
     * @return Brightness value (0-255)
     */
    uint8_t getBrightness() const { return brightness.load(); }

    /**
     * Get current show name
     * @return Show name
     */
    std::string getCurrentShowName() const;

    /**
     * Get current cycle time
     * @return Cycle time in ms
     */
    uint16_t getCycleTime() const;

    /**
     * Clear the LED strip (turn all LEDs off)
     * Used before factory reset or shutdown
     */
    void clearStrip();

    /**
     * Destructor
     */
    ~ShowController();

    const std::vector<ShowFactory::ShowInfo> &listShows() const;

    void executeShow(unsigned int iteration) const;

    void show() const;

    /**
     * Check if the current show has reached a static state
     * Used for power save mode
     * @return true if show is static (no animation), false otherwise
     */
    bool isShowComplete() const;

    /**
     * Update show statistics
     * @param stats New statistics
     */
    void updateStats(const ShowStats &stats);

    /**
     * Get current show statistics
     * @return Current statistics
     */
    ShowStats getStats() const;
};

#endif //LEDZ_SHOWCONTROLLER_H
