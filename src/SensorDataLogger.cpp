#include "SensorDataLogger.h"
#include <algorithm>

SensorDataLogger::SensorDataLogger(size_t maxEntries)
    : maxEntries(maxEntries), buffer(maxEntries) {
    buffer.resize(maxEntries);
}

void SensorDataLogger::addReading(const Sensor::SensorData &data) {
    if (maxEntries == 0) return;
    
    uint32_t now = millis();
    
    // Store the reading
    buffer[currentIndex].data = data;
    buffer[currentIndex].logTime = now;
    
    // Update counters
    if (entryCount < maxEntries) {
        entryCount++;
    } else {
        wrapped = true;
    }
    
    // Move to next position
    currentIndex = (currentIndex + 1) % maxEntries;
}

std::vector<SensorDataLogger::LogEntry> SensorDataLogger::getAllEntries() const {
    std::vector<LogEntry> result;
    result.reserve(entryCount);
    
    if (entryCount == 0) return result;
    
    if (!wrapped) {
        // Simple case: buffer hasn't wrapped, return entries from oldest to newest
        for (size_t i = 0; i < entryCount; i++) {
            result.push_back(buffer[i]);
        }
    } else {
        // Buffer has wrapped: return from currentIndex to end, then from beginning to currentIndex-1
        for (size_t i = currentIndex; i < maxEntries; i++) {
            result.push_back(buffer[i]);
        }
        for (size_t i = 0; i < currentIndex; i++) {
            result.push_back(buffer[i]);
        }
    }
    
    return result;
}

std::vector<SensorDataLogger::LogEntry> SensorDataLogger::getRecentEntries(size_t count) const {
    std::vector<LogEntry> result;
    if (entryCount == 0 || count == 0) return result;
    
    count = std::min(count, entryCount);
    result.reserve(count);
    
    if (!wrapped) {
        // Start from the oldest entry that's still in buffer
        size_t start = entryCount >= count ? entryCount - count : 0;
        for (size_t i = start; i < entryCount; i++) {
            result.push_back(buffer[i]);
        }
    } else {
        // Buffer has wrapped, need to handle circular buffer
        if (count <= currentIndex) {
            // All requested entries are in the beginning of the buffer
            for (size_t i = currentIndex - count; i < currentIndex; i++) {
                result.push_back(buffer[i]);
            }
        } else {
            // Entries span the wrap-around point
            size_t firstPart = currentIndex;
            size_t secondPart = count - firstPart;
            
            for (size_t i = maxEntries - secondPart; i < maxEntries; i++) {
                result.push_back(buffer[i]);
            }
            for (size_t i = 0; i < firstPart; i++) {
                result.push_back(buffer[i]);
            }
        }
    }
    
    return result;
}

std::vector<SensorDataLogger::LogEntry> SensorDataLogger::getEntriesInRange(uint32_t startTime, uint32_t endTime) const {
    std::vector<LogEntry> result;
    if (entryCount == 0) return result;
    
    // Get all entries and filter by time range
    auto allEntries = getAllEntries();
    for (const auto &entry : allEntries) {
        if (entry.logTime >= startTime && entry.logTime <= endTime) {
            result.push_back(entry);
        }
    }
    
    return result;
}

void SensorDataLogger::clear() {
    entryCount = 0;
    currentIndex = 0;
    wrapped = false;
}

String SensorDataLogger::exportAsCSV() const {
    String csv = "timestamp,temperature,humidity,valid\n";
    
    auto entries = getAllEntries();
    for (const auto &entry : entries) {
        csv += String(entry.logTime) + ",";
        csv += String(entry.data.temperature, 1) + ",";
        csv += String(entry.data.humidity, 1) + ",";
        csv += String(entry.data.valid ? "1" : "0") + "\n";
    }
    
    return csv;
}