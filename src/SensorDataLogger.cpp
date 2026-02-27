#include "SensorDataLogger.h"
#include <algorithm>
#include <cstring>

SensorDataLogger::SensorDataLogger(size_t maxEntries)
    : maxEntries(maxEntries), buffer(maxEntries) {
    buffer.resize(maxEntries);
}

void SensorDataLogger::addReading(const std::vector<Sensor::Measurement> &measurements, uint32_t timestamp) {
    if (maxEntries == 0) return;

    uint32_t now = millis();

    buffer[currentIndex].measurements = measurements;
    buffer[currentIndex].timestamp = timestamp;
    buffer[currentIndex].logTime = now;

    if (entryCount < maxEntries) {
        entryCount++;
    } else {
        wrapped = true;
    }

    currentIndex = (currentIndex + 1) % maxEntries;
}

std::vector<SensorDataLogger::LogEntry> SensorDataLogger::getAllEntries() const {
    std::vector<LogEntry> result;
    result.reserve(entryCount);

    if (entryCount == 0) return result;

    if (!wrapped) {
        for (size_t i = 0; i < entryCount; i++) {
            result.push_back(buffer[i]);
        }
    } else {
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
        size_t start = entryCount >= count ? entryCount - count : 0;
        for (size_t i = start; i < entryCount; i++) {
            result.push_back(buffer[i]);
        }
    } else {
        if (count <= currentIndex) {
            for (size_t i = currentIndex - count; i < currentIndex; i++) {
                result.push_back(buffer[i]);
            }
        } else {
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

namespace {
    float findMeasurementValue(const std::vector<Sensor::Measurement> &measurements, const char* type) {
        for (const auto &m : measurements) {
            if (strcmp(m.type, type) == 0) {
                return m.value;
            }
        }
        return 0.0f;
    }
}

String SensorDataLogger::exportAsCSV() const {
    String csv = "timestamp,temperature,humidity\n";

    auto entries = getAllEntries();
    for (const auto &entry : entries) {
        float temp = findMeasurementValue(entry.measurements, "temperature");
        float hum = findMeasurementValue(entry.measurements, "humidity");
        csv += String(entry.logTime) + ",";
        csv += String(temp, 1) + ",";
        csv += String(hum, 1) + "\n";
    }

    return csv;
}
