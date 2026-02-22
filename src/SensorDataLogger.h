#ifndef SENSOR_DATA_LOGGER_H
#define SENSOR_DATA_LOGGER_H

#include <vector>
#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "sensor/Sensor.h"

/**
 * Sensor Data Logger - Stores historical sensor readings
 */
class SensorDataLogger {
private:
    struct LogEntry {
        Sensor::SensorData data;
        uint32_t logTime; // Time when this entry was logged
    };
    
    std::vector<LogEntry> buffer;
    size_t maxEntries;
    size_t currentIndex = 0;
    size_t entryCount = 0;
    bool wrapped = false;
    
public:
    /**
     * Constructor
     * @param maxEntries Maximum number of entries to store
     */
    explicit SensorDataLogger(size_t maxEntries = 1000);
    
    /**
     * Add a new sensor reading to the log
     * @param data Sensor data to log
     */
    void addReading(const Sensor::SensorData &data);
    
    /**
     * Get all logged entries
     * @return Vector of all log entries (newest first)
     */
    std::vector<LogEntry> getAllEntries() const;
    
    /**
     * Get recent entries
     * @param count Maximum number of entries to return
     * @return Vector of recent log entries (newest first)
     */
    std::vector<LogEntry> getRecentEntries(size_t count) const;
    
    /**
     * Get entries within a time range
     * @param startTime Start time (milliseconds since boot)
     * @param endTime End time (milliseconds since boot)
     * @return Vector of log entries in the time range (oldest first)
     */
    std::vector<LogEntry> getEntriesInRange(uint32_t startTime, uint32_t endTime) const;
    
    /**
     * Clear all logged data
     */
    void clear();
    
    /**
     * Get current number of entries
     * @return Number of entries in the log
     */
    size_t getEntryCount() const { return entryCount; }
    
    /**
     * Get maximum capacity
     * @return Maximum number of entries the log can hold
     */
    size_t getMaxCapacity() const { return maxEntries; }
    
    /**
     * Check if buffer has wrapped around
     * @return True if buffer has wrapped around (oldest data has been overwritten)
     */
    bool hasWrapped() const { return wrapped; }
    
    /**
     * Export data as CSV
     * @return CSV string with timestamp, temperature, humidity
     */
    String exportAsCSV() const;
};

#endif // SENSOR_DATA_LOGGER_H