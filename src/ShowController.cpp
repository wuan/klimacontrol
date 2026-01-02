#include "ShowController.h"
#include "color.h"

#include <cstring> // strlen, strncpy

#ifdef ARDUINO
#include <Arduino.h>
#endif

ShowController::ShowController(ShowFactory &factory, Config::ConfigManager &config)
    : factory(factory), config(config), brightness(128),
      layout(), baseStrip()
#ifdef ARDUINO
      , commandQueue(nullptr)
#endif
{
}

void ShowController::begin() {
#ifdef ARDUINO
    // Create FreeRTOS queue (5 commands deep)
    commandQueue = xQueueCreate(5, sizeof(ShowCommand));

    if (commandQueue == nullptr) {
        Serial.println("ERROR: Failed to create show command queue!");
        return;
    }
#endif
    Config::ShowConfig showConfig = config.loadShowConfig();
    Config::DeviceConfig deviceConfig = config.loadDeviceConfig();

    // Apply loaded configuration
    brightness = deviceConfig.brightness;

#ifdef ARDUINO
    Serial.println("ShowController: preparing show");
#endif
    // Create initial show
    const char *initialShowName = showConfig.current_show;

    if (strlen(initialShowName) > 0 && factory.hasShow(initialShowName)) {
        currentShowName = initialShowName;
    } else {
        currentShowName = "Rainbow";
    }

    // Load parameters if available
    const char *params = (strlen(showConfig.params_json) > 0) ? showConfig.params_json : "{}";
#ifdef ARDUINO
    Serial.printf("ShowController: Creating initial show %s with params %s\n", currentShowName.c_str(), params);
#endif
    currentShow = factory.createShow(currentShowName, params);
#ifdef ARDUINO
    Serial.printf("ShowController: Show %s created\n", currentShowName.c_str());
    Serial.printf("ShowController: Initial show %p\n", currentShow.get());
#endif

    if (currentShow) {
#ifdef ARDUINO
        Serial.print("ShowController: Initial show loaded: ");
        Serial.print(currentShowName.c_str());
        Serial.print(" with params: ");
        Serial.println(params);
#endif
    } else {
#ifdef ARDUINO
        Serial.println("ERROR: Failed to create initial show!");
#endif
    }
}

bool ShowController::queueShowChange(const std::string &showName, const std::string &paramsJson) {
#ifdef ARDUINO
    if (commandQueue == nullptr) {
        return false;
    }

    ShowCommand cmd;
    cmd.type = ShowCommandType::SET_SHOW;
    cmd.show_name = strdup(showName.c_str());
    cmd.params_json = strdup(paramsJson.c_str());

    // Try to send with no wait (non-blocking)
    if (xQueueSend(commandQueue, &cmd, 0) == pdTRUE) {
        return true;
    }

    // If failed to queue, we must free the memory
    free(cmd.show_name);
    free(cmd.params_json);

    Serial.println("WARNING: Show command queue full!");
    return false;
#else
    return false;
#endif
}

bool ShowController::queueBrightnessChange(uint8_t brightness) {
#ifdef ARDUINO
    if (commandQueue == nullptr) {
        return false;
    }

    ShowCommand cmd;
    cmd.type = ShowCommandType::SET_BRIGHTNESS;
    cmd.brightness_value = brightness;

    if (xQueueSend(commandQueue, &cmd, 0) == pdTRUE) {
        return true;
    }

    Serial.println("WARNING: Brightness command queue full!");
    return false;
#else
    return false;
#endif
}

void ShowController::applyCommand(const ShowCommand &cmd) {
    switch (cmd.type) {
        case ShowCommandType::SET_SHOW: {
            // Create new show with parameters
            std::unique_ptr<Show::Show> newShow = factory.createShow(cmd.show_name, cmd.params_json);
            if (newShow != nullptr) {
                currentShow = std::move(newShow);
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    currentShowName = cmd.show_name;
                }

#ifdef ARDUINO
                Serial.print("ShowController: Switched to show: ");
                Serial.print(currentShowName.c_str());
                Serial.print(" with params: ");
                Serial.println(cmd.params_json);
#endif

                // Save to configuration
                Config::ShowConfig showConfig = config.loadShowConfig();
                strncpy(showConfig.current_show, currentShowName.c_str(), sizeof(showConfig.current_show) - 1);
                showConfig.current_show[sizeof(showConfig.current_show) - 1] = '\0';
                strncpy(showConfig.params_json, cmd.params_json, sizeof(showConfig.params_json) - 1);
                showConfig.params_json[sizeof(showConfig.params_json) - 1] = '\0';
                config.saveShowConfig(showConfig);
            } else {
#ifdef ARDUINO
                Serial.print("ERROR: Failed to create show: ");
                Serial.println(cmd.show_name);
#endif
            }
            break;
        }

        case ShowCommandType::SET_BRIGHTNESS: {
            brightness.store(cmd.brightness_value);

#ifdef ARDUINO
            Serial.print("ShowController: Brightness set to: ");
            Serial.println(brightness.load());
#endif

            // Save to configuration
            Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
            deviceConfig.brightness = brightness.load();
            config.saveDeviceConfig(deviceConfig);
            break;
        }

        case ShowCommandType::SET_LAYOUT: {
#ifdef ARDUINO
            if (layout != nullptr && baseStrip != nullptr) {
                // Recreate layout with new parameters
                layout = std::make_unique<Strip::Layout>(*baseStrip, cmd.layout_reverse,
                                                         cmd.layout_mirror, cmd.layout_dead_leds);

                Serial.printf("ShowController: Layout updated - reverse=%d, mirror=%d, dead_leds=%u\n",
                              cmd.layout_reverse, cmd.layout_mirror, cmd.layout_dead_leds);

                // Save to configuration
                Config::LayoutConfig layoutConfig;
                layoutConfig.reverse = cmd.layout_reverse;
                layoutConfig.mirror = cmd.layout_mirror;
                layoutConfig.dead_leds = cmd.layout_dead_leds;
                config.saveLayoutConfig(layoutConfig);

                // Restart current show to pick up new layout dimensions
                Config::ShowConfig showConfig = config.loadShowConfig();
                std::unique_ptr<Show::Show> newShow = factory.createShow(currentShowName, showConfig.params_json);
                if (newShow != nullptr) {
                    currentShow = std::move(newShow);
                    Serial.printf("ShowController: Restarted show '%s' with updated layout\n", currentShowName.c_str());
                }
            } else {
                Serial.println("ERROR: Layout pointers not set!");
            }
#endif
            break;
        }

        case ShowCommandType::LOAD_PRESET: {
#ifdef ARDUINO
            Serial.printf("ShowController: Loading preset - show=%s\n", cmd.show_name);

            // 1. Update layout if we have valid strip pointers
            if (layout != nullptr && baseStrip != nullptr) {
                layout = std::make_unique<Strip::Layout>(*baseStrip, cmd.layout_reverse,
                                                         cmd.layout_mirror, cmd.layout_dead_leds);

                Serial.printf("ShowController: Preset layout - reverse=%d, mirror=%d, dead_leds=%d\n",
                              cmd.layout_reverse, cmd.layout_mirror, cmd.layout_dead_leds);

                // Save layout config
                Config::LayoutConfig layoutConfig;
                layoutConfig.reverse = cmd.layout_reverse;
                layoutConfig.mirror = cmd.layout_mirror;
                layoutConfig.dead_leds = cmd.layout_dead_leds;
                config.saveLayoutConfig(layoutConfig);
            }

            // 2. Create show with preset parameters
            std::unique_ptr<Show::Show> newShow = factory.createShow(cmd.show_name, cmd.params_json);
            if (newShow != nullptr) {
                currentShow = std::move(newShow);
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    currentShowName = cmd.show_name;
                }

                Serial.printf("ShowController: Preset show '%s' loaded with params: %s\n",
                              currentShowName.c_str(), cmd.params_json);

                // Save show config
                Config::ShowConfig showConfig = config.loadShowConfig();
                strncpy(showConfig.current_show, currentShowName.c_str(), sizeof(showConfig.current_show) - 1);
                strncpy(showConfig.params_json, cmd.params_json, sizeof(showConfig.params_json) - 1);
                config.saveShowConfig(showConfig);
            } else {
                Serial.printf("ERROR: Failed to create preset show: %s\n", cmd.show_name);
            }
#endif
            break;
        }
    }
}

void ShowController::processCommands() {
#ifdef ARDUINO
    if (commandQueue == nullptr) {
        return;
    }

    // Process all pending commands (non-blocking)
    ShowCommand cmd;
    while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
        applyCommand(cmd);
        // Free memory allocated for pointers in the command
        if (cmd.type == ShowCommandType::SET_SHOW || cmd.type == ShowCommandType::LOAD_PRESET) {
            free(cmd.show_name);
            free(cmd.params_json);
        }
    }
#endif
}

bool ShowController::queueLayoutChange(bool reverse, bool mirror, int16_t dead_leds) {
#ifdef ARDUINO
    if (commandQueue == nullptr) {
        return false;
    }

    ShowCommand cmd;
    cmd.type = ShowCommandType::SET_LAYOUT;
    cmd.layout_reverse = reverse;
    cmd.layout_mirror = mirror;
    cmd.layout_dead_leds = dead_leds;

    if (xQueueSend(commandQueue, &cmd, 0) == pdTRUE) {
        return true;
    }

    Serial.println("WARNING: Layout command queue full!");
    return false;
#else
    return false;
#endif
}

bool ShowController::queuePresetLoad(const Config::Preset &preset) {
#ifdef ARDUINO
    if (commandQueue == nullptr) {
        return false;
    }

    ShowCommand cmd;
    cmd.type = ShowCommandType::LOAD_PRESET;

    // ShowCommand uses pointers; Preset stores C strings.
    cmd.show_name = strdup(preset.show_name);
    cmd.params_json = strdup(preset.params_json);

    cmd.layout_reverse = preset.layout_reverse;
    cmd.layout_mirror = preset.layout_mirror;
    cmd.layout_dead_leds = preset.layout_dead_leds;

    if (xQueueSend(commandQueue, &cmd, 0) == pdTRUE) {
        Serial.printf("ShowController: Queued preset load '%s'\n", preset.name);
        return true;
    }

    // If failed to queue, we must free the memory
    free(cmd.show_name);
    free(cmd.params_json);

    Serial.println("WARNING: Preset load command queue full!");
    return false;
#else
    return false;
#endif
}

void ShowController::setStrip(std::unique_ptr<Strip::Strip> &&base) {
    baseStrip = std::move(base);

    if (baseStrip) {
        // Load and apply layout configuration
#ifdef ARDUINO
        Serial.println("ShowController: Loading layout configuration...");
#endif
        Config::LayoutConfig layoutConfig = config.loadLayoutConfig();
        layout = std::make_unique<Strip::Layout>(*baseStrip, layoutConfig.reverse, layoutConfig.mirror,
                                                 layoutConfig.dead_leds);
#ifdef ARDUINO
        Serial.printf("Layout initialized: reverse=%d, mirror=%d, dead_leds=%u\n",
                      layoutConfig.reverse, layoutConfig.mirror, layoutConfig.dead_leds);
#endif
    } else {
#ifdef ARDUINO
        Serial.println("ERROR: Failed to initialize layout! base strip not set");
#endif
    }
}

void ShowController::clearStrip() {
#ifdef ARDUINO
    if (layout) {
        Strip::Color black = color(0, 0, 0);
        layout->fill(black);
        layout->show();

        Serial.println("ShowController: Strip cleared");
    }
#endif
}

ShowController::~ShowController() {
#ifdef ARDUINO
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
#endif
}

const std::vector<ShowFactory::ShowInfo> &ShowController::listShows() const {
    return factory.listShows();
}

uint16_t ShowController::getCycleTime() const {
    return config.loadDeviceConfig().cycle_time;
}

void ShowController::executeShow(unsigned int iteration) const {
    if (layout && currentShow) {
        layout->setBrightness(brightness.load());
        currentShow->execute(*layout, iteration);
    }
}

void ShowController::show() const {
    if (layout) {
        layout->show();
    }
}

bool ShowController::isShowComplete() const {
    return currentShow && currentShow->isComplete();
}

void ShowController::updateStats(const ShowStats &newStats) {
    std::lock_guard<std::mutex> lock(stateMutex);
    stats = newStats;
}

ShowStats ShowController::getStats() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return stats;
}

std::string ShowController::getCurrentShowName() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentShowName;
}
