//
// Timer Scheduler for klimacontrol
// Manages countdown timers and alarms
//

#ifndef KLIMACONTROL_TIMER_SCHEDULER_H
#define KLIMACONTROL_TIMER_SCHEDULER_H

#include "Config.h"

#include "SensorController.h"

class TimerScheduler {
private:
    Config::ConfigManager &config;
    SensorController &sensorController;
    Config::TimersConfig timersConfig;
    bool ntpAvailable = false;

    /**
     * Execute a timer action
     */
    void executeTimer(uint8_t index);

public:
    /**
     * Get seconds since midnight for the given epoch time
     */
    [[nodiscard]] uint32_t getSecondsSinceMidnight(uint32_t epochTime) const;

    /**
     * Constructor
     * @param config Configuration manager reference
     * @param showController Show controller reference
     */
    TimerScheduler(Config::ConfigManager &config, SensorController &sensorController);

    /**
     * Initialize the scheduler - loads config from NVS
     */
    void begin();

    /**
     * Check all timers and execute any that have triggered
     * Called every second from Network task
     * @param currentEpoch Current NTP epoch time
     */
    void checkTimers(uint32_t currentEpoch);

    /**
     * Set NTP availability status
     * @param available true if NTP time is available
     */
    void setNtpAvailable(bool available) { ntpAvailable = available; }

    /**
     * Set a countdown timer
     * @param index Timer slot index (0-3)
     * @param durationSeconds Countdown duration in seconds
     * @param action Action to perform when timer expires
     * @param presetIndex Preset index (only used if action is LOAD_PRESET)
     * @param currentEpoch Current NTP epoch time
     * @return true if timer was set successfully
     */
    bool setCountdown(uint8_t index, uint32_t durationSeconds, Config::TimerAction action,
                      uint8_t presetIndex, uint32_t currentEpoch);

    /**
     * Set a daily recurring alarm
     * @param index Timer slot index (0-3)
     * @param secondsSinceMidnight Time of day as seconds since midnight
     * @param action Action to perform when alarm triggers
     * @param presetIndex Preset index (only used if action is LOAD_PRESET)
     * @return true if alarm was set successfully
     */
    bool setDailyAlarm(uint8_t index, uint32_t secondsSinceMidnight, Config::TimerAction action,
                       uint8_t presetIndex);

    /**
     * Cancel a timer
     * @param index Timer slot index (0-3)
     * @return true if timer was cancelled
     */
    bool cancelTimer(uint8_t index);

    /**
     * Get remaining seconds for a timer
     * @param index Timer slot index (0-3)
     * @param currentEpoch Current NTP epoch time
     * @return Remaining seconds, or 0 if timer is not active
     */
    [[nodiscard]] uint32_t getRemainingSeconds(uint8_t index, uint32_t currentEpoch) const;

    /**
     * Get the timers configuration (for API access)
     * @return Reference to timers configuration
     */
    [[nodiscard]] const Config::TimersConfig& getTimersConfig() const { return timersConfig; }

    /**
     * Set timezone offset
     * @param offsetHours Timezone offset in hours from UTC (-12 to +14)
     */
    void setTimezoneOffset(int8_t offsetHours);

    /**
     * Get timezone offset
     * @return Timezone offset in hours
     */
    [[nodiscard]] int8_t getTimezoneOffset() const { return timersConfig.timezone_offset_hours; }
};

#endif //KLIMACONTROL_TIMER_SCHEDULER_H
