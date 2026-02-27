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
public:
    struct LogEntry {
        std::vector<Sensor::Measurement> measurements;
        uint32_t timestamp;
        uint32_t logTime;
    };

private:
    std::vector<LogEntry> buffer;
    size_t maxEntries;
    size_t currentIndex = 0;
    size_t entryCount = 0;
    bool wrapped = false;

public:
    explicit SensorDataLogger(size_t maxEntries = 1000);

    void addReading(const std::vector<Sensor::Measurement> &measurements, uint32_t timestamp);

    std::vector<LogEntry> getAllEntries() const;
    std::vector<LogEntry> getRecentEntries(size_t count) const;
    std::vector<LogEntry> getEntriesInRange(uint32_t startTime, uint32_t endTime) const;

    void clear();

    size_t getEntryCount() const { return entryCount; }
    size_t getMaxCapacity() const { return maxEntries; }
    bool hasWrapped() const { return wrapped; }

    String exportAsCSV() const;
};

#endif // SENSOR_DATA_LOGGER_H
