#include "SensorController.h"
#include "sensor/DeviceSensor.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <variant>

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/semphr.h>
#include "I2CBus.h"
#endif

#ifdef ARDUINO
static const char* TAG = "sensor";
#else
// On native, the ESP_LOG* macros are no-ops; their tag argument is
// discarded. Define TAG as a macro so there is no unused-variable to
// warn about.
#define TAG "sensor"
#endif

namespace {
    // Upper bound on measurements per sensor in a single read cycle. Used
    // to pre-reserve `currentMeasurements` capacity at boot so the per-cycle
    // assignment does not reallocate. Generous (8) so a sensor that adds
    // extra derived measurements (dew point, sea-level pressure, etc.) still
    // fits without growing the vector. See spec `memory-management` →
    // "Vector capacities are reserved at boot" for the contract.
    constexpr size_t MAX_MEASUREMENTS_PER_SENSOR = 8;
}

SensorController::SensorController(Config::ConfigManager &config, [[maybe_unused]] DarkModeStatusLed *statusLed)
    : config(config), lastReadingTimestamp(0), dataValid(false),
#ifdef ARDUINO
      dataMutex(xSemaphoreCreateMutex()),
      statusLed(statusLed),
#endif
      lastReadingTime(0) {
#ifdef ARDUINO
    // xSemaphoreCreateMutex() returns nullptr if the heap is exhausted at boot.
    // Previously this just logged a warning and continued, which let the
    // device appear healthy while producing no real sensor data (silent
    // degradation). Now we fail hard: drive the LED to ERROR, hold briefly so
    // a human can see it, then restart.
    if (!dataMutex) {
        ESP_LOGE(TAG, "Failed to create dataMutex (out of memory) — restarting");
        if (statusLed) {
            statusLed->setState(LedState::ERROR); // applies immediately
        }
        delay(5000);
        ESP.restart();
    }
#endif
}

bool SensorController::didFailMutexInit() const {
#ifdef ARDUINO
    return dataMutex == nullptr;
#else
    // On native the mutex is not allocated at all (no FreeRTOS), so report
    // "would have failed" only if a test explicitly simulated it. The native
    // build never produces a real allocation to inspect, so this is always
    // false in practice — the accessor exists so the failure path is
    // observable as a single call site across both environments.
    return false;
#endif
}

void SensorController::begin() {
    ESP_LOGI(TAG, "Beginning sensor initialization...");

    sortSensors();

    // add local device metrics sensor (RSSI, chip temp, free heap, uptime)
    sensors.push_back(std::make_unique<Sensor::DeviceSensor>());

    // Initialize all sensors
    for (auto &sensor : sensors) {
        if (sensor) {
            ESP_LOGI(TAG, "Initializing sensor %s...", sensor->getType());
            if (sensor->tryBegin()) {
                ESP_LOGI(TAG, "Successfully initialized sensor %s", sensor->getType());
            } else {
                ESP_LOGW(TAG, "Failed to initialize sensor %s", sensor->getType());
            }
        }
    }

    ESP_LOGI(TAG, "Found %u sensors total", sensors.size());
}

void SensorController::addSensor(std::unique_ptr<Sensor::Sensor> sensor) {
    if (sensor) {
        sensors.push_back(std::move(sensor));
    }
}

void SensorController::reserveSensorSlots(size_t n) {
    // Reserve the sensor list so the I2C scan loop's addSensor() calls never
    // reallocate. The currentMeasurements vector is sized to hold the worst
    // case (every sensor returning its full measurementCount() + the per-sensor
    // Time entry added by readSensors()) so the per-cycle assignment in
    // readSensors() also does not reallocate.
    sensors.reserve(n);
    currentMeasurements.reserve(n * MAX_MEASUREMENTS_PER_SENSOR);
    // begin() appends the DeviceSensor after the scan, hence the +1. The slot
    // vectors themselves are reserved when the slot is first created in
    // readSensors(), so they never grow at runtime either.
    slots.reserve(n + 1);
}

void SensorController::collectValidSlots(std::vector<Sensor::Measurement> &out) const {
    for (const auto &slot : slots) {
        if (!slot.valid) continue;
        out.insert(out.end(), slot.measurements.begin(), slot.measurements.end());
    }
}

void SensorController::sortSensors() {
    // Topological sort: repeatedly pick sensors whose requiresMeasurements() are satisfied
    // by already-placed sensors' providesMeasurements(). Simple quadratic — only 3-5 sensors.
    std::vector<std::unique_ptr<Sensor::Sensor>> sorted;
    sorted.reserve(sensors.size());

    std::vector<bool> placed(sensors.size(), false);

    for (size_t round = 0; round < sensors.size(); ++round) {
        bool progress = false;
        for (size_t i = 0; i < sensors.size(); ++i) {
            if (placed[i]) continue;

            Sensor::TypeSpan reqs = sensors[i]->requiresMeasurements();
            bool satisfied = true;

            for (uint8_t r = 0; r < reqs.count && satisfied; ++r) {
                bool found = std::any_of(sorted.begin(), sorted.end(), [&reqs, r](const auto& s) {
                    const Sensor::TypeSpan prov = s->providesMeasurements();
                    return std::find(prov.data, prov.data + prov.count, reqs.data[r]) != prov.data + prov.count;
                });
                if (!found) satisfied = false;
            }

            if (satisfied) {
                ESP_LOGD(TAG, "Read order [%u] %s",
                         sorted.size(), sensors[i]->getType());
                sorted.push_back(std::move(sensors[i]));
                placed[i] = true;
                progress = true;
            }
        }
        if (!progress) break;
    }

    // Append any sensors with unmet dependencies (with warning)
    for (size_t i = 0; i < sensors.size(); ++i) {
        if (!placed[i]) {
            ESP_LOGW(TAG, "%s has unmet dependencies, appending last",
                     sensors[i]->getType());
            sorted.push_back(std::move(sensors[i]));
        }
    }

    sensors = std::move(sorted);
}

void SensorController::readSensors() {
    readSensors(millis());
}

void SensorController::readSensors(uint32_t nowMs) {
    const uint32_t timestamp = nowMs;
    std::vector<Sensor::Measurement> allMeasurements;
    bool anyValid = false;       // at least one sensor read valid *this* tick
#ifdef ARDUINO
    bool anyI2CAttempted = false; // at least one I2C sensor was due and read this cycle
    bool anyI2CValid = false;     // at least one I2C sensor read valid this cycle
#endif

    // Keep one slot per sensor. Sensors are only ever appended (addSensor,
    // begin's DeviceSensor) or reordered by sortSensors() before the first
    // read, when every slot is still empty, so index alignment holds.
    while (slots.size() < sensors.size()) {
        slots.emplace_back();
        slots.back().measurements.reserve(MAX_MEASUREMENTS_PER_SENSOR);
    }

    // The shared phase for default-interval sensors: first tick ever, then
    // every MEASUREMENT_INTERVAL_MS. Rebased to `now` rather than advanced by
    // the interval, so a late tick shifts the phase instead of double-reading.
    const bool defaultDue = !defaultCycleRun ||
                            (nowMs - lastDefaultCycleMs >= MEASUREMENT_INTERVAL_MS);
    bool anyDefaultSensor = false;

    // ===== PHASE 1: Sensor I2C reads (I2C bus locked) =====
    {
#ifdef ARDUINO
        // Hold the I2C bus for the sensor read cycle so the web /api/i2c/scan can't
        // interleave transactions and corrupt a sensor reading (see I2CBus.h).
        // If the bus is held elsewhere, skip this cycle rather than block the sensor task.
        I2CBus::Lock bus(pdMS_TO_TICKS(100));
        if (!bus) {
            ESP_LOGW(TAG, "I2C bus busy - skipping read cycle");
            return;
        }
#endif

        // Retry failed sensors periodically
        static constexpr uint32_t RETRY_INTERVAL_MS = 30000;
        for (auto &sensor : sensors) {
            if (!sensor) continue;
            auto status = sensor->getStatus();
            if (status == Sensor::SensorStatus::InitFailed ||
                status == Sensor::SensorStatus::ReadFailing) {
                if (timestamp - sensor->getLastInitAttempt() >= RETRY_INTERVAL_MS) {
                    ESP_LOGI(TAG, "Retrying init for %s...", sensor->getType());
                    if (sensor->tryBegin()) {
                        ESP_LOGI(TAG, "%s now online", sensor->getType());
                    }
                }
            }
        }

        Sensor::ReadConfig readConfig;
        readConfig.elevation = config.getDeviceConfig().elevation;

        // `prior` for dependent sensors is the union of what we currently know,
        // refreshed after every successful read so a same-tick provider is
        // seen fresh and an off-tick provider is seen from its cache slot.
        std::vector<Sensor::Measurement> prior;
        prior.reserve(sensors.size() * MAX_MEASUREMENTS_PER_SENSOR);
        collectValidSlots(prior);

        for (size_t i = 0; i < sensors.size(); ++i) {
            auto &sensor = sensors[i];
            if (!sensor) continue;
            SensorSlot &slot = slots[i];

            const uint32_t required = sensor->requiredIntervalMs();
            const bool isDefault = required == 0;
            if (isDefault) anyDefaultSensor = true;

            // Only read sensors that are online
            if (sensor->getStatus() != Sensor::SensorStatus::Online) {
                continue;
            }

            const bool due = isDefault
                ? defaultDue
                : (!slot.everRead || nowMs - slot.lastReadMs >= required);
            if (!due) continue;

            slot.lastReadMs = nowMs;
            slot.everRead = true;
#ifdef ARDUINO
            if (sensor->usesI2C()) anyI2CAttempted = true;
#endif

            uint32_t readStart = millis();
            Sensor::SensorReading reading = sensor->read(readConfig, prior);
            uint32_t readTime = millis() - readStart;

            sensor->recordReadResult(reading.valid);

            if (reading.valid) {
                slot.measurements.clear();
                for (const auto &m : reading.measurements) {
                    slot.measurements.push_back(m);
                }
                slot.measurements.push_back({Sensor::MeasurementType::Time, (int32_t)readTime, sensor->getType(), false});
                slot.lastValidMs = nowMs;
                slot.valid = true;
                anyValid = true;
#ifdef ARDUINO
                if (sensor->usesI2C()) anyI2CValid = true;
#endif
                prior.clear();
                collectValidSlots(prior);
            } else {
                ESP_LOGW(TAG, "Sensor %s - invalid data", sensor->getType());
            }
        }

        if (defaultDue && anyDefaultSensor) {
            lastDefaultCycleMs = nowMs;
            defaultCycleRun = true;
        }

#ifdef ARDUINO
        // I2C bus recovery: if I2C sensors were attempted but none produced a
        // valid reading this cycle, the bus may be wedged (a slave stuck holding
        // SDA low). After a short streak, attempt recovery while we still hold the
        // bus lock so no scan can interleave. Ticks on which no I2C sensor was
        // due are neither a success nor a failure. The DeviceSensor is not I2C,
        // so it never masks this condition.
        if (anyI2CAttempted && !anyI2CValid) {
            if (++consecutiveI2CFailures >= I2C_RECOVERY_FAILURE_STREAK) {
                ESP_LOGW(TAG, "%u consecutive I2C read cycles failed - attempting bus recovery",
                         consecutiveI2CFailures);
                if (I2CBus::recover()) {
                    ESP_LOGI(TAG, "I2C bus recovery succeeded (SDA released)");
                } else {
                    ESP_LOGE(TAG, "I2C bus recovery failed - SDA still held low");
                }
                consecutiveI2CFailures = 0;
            }
        } else if (anyI2CValid) {
            consecutiveI2CFailures = 0;
        }
#endif
    }  // I2C bus lock released here

    // ===== PHASE 2: Expire stale slots, build the union =====
    //
    // A slot is dropped when its sensor is no longer Online, or when the
    // reading has outlived SLOT_EXPIRY_INTERVALS of that sensor's interval.
    // The latter bounds staleness regardless of how slowly the driver's
    // failure counter reaches ReadFailing.
    bool anySlotValid = false;
    for (size_t i = 0; i < sensors.size(); ++i) {
        SensorSlot &slot = slots[i];
        if (!slot.valid) continue;
        const auto &sensor = sensors[i];
        const bool online = sensor && sensor->getStatus() == Sensor::SensorStatus::Online;
        const bool expired = online &&
            nowMs - slot.lastValidMs > SLOT_EXPIRY_INTERVALS * effectiveIntervalMs(*sensor);
        if (!online || expired) {
            ESP_LOGW(TAG, "Sensor %s - dropping cached reading (%s)",
                     sensor ? sensor->getType() : "?", expired ? "expired" : "offline");
            slot.valid = false;
            slot.measurements.clear();
            continue;
        }
        anySlotValid = true;
    }

    allMeasurements.reserve(sensors.size() * MAX_MEASUREMENTS_PER_SENSOR);
    collectValidSlots(allMeasurements);

    // ===== PHASE 3: Publish (I2C bus NOT locked) =====
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
#endif
        // Swap rather than move-assign: the reserved capacity of
        // currentMeasurements (see reserveSensorSlots) stays with the buffer
        // that readers copy from, and the outgoing one is freed here.
        currentMeasurements.clear();
        currentMeasurements.insert(currentMeasurements.end(),
                                   allMeasurements.begin(), allMeasurements.end());
        dataValid = anySlotValid;
        if (anyValid) {
            lastReadingTimestamp = timestamp;
            lastReadingTime = timestamp;
        }
#ifdef ARDUINO
        xSemaphoreGive(dataMutex);
    }
#endif

}

std::vector<Sensor::Measurement> SensorController::getMeasurements() const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto copy = currentMeasurements;
        xSemaphoreGive(dataMutex);
        return copy;
    }
    return {};
#else
    return currentMeasurements;
#endif
}

SensorController::Snapshot SensorController::getSnapshot() const {
    Snapshot snap;
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap.valid = dataValid;
        snap.timestamp = lastReadingTimestamp;
        if (dataValid) snap.measurements = currentMeasurements;
        xSemaphoreGive(dataMutex);
    }
#else
    snap.valid = dataValid;
    snap.timestamp = lastReadingTimestamp;
    if (dataValid) snap.measurements = currentMeasurements;
#endif
    return snap;
}

SensorController::ProcessValue SensorController::getProcessValue() const {
    ProcessValue pv;
    // Same lock discipline as getSnapshot(), but no vector copy: the two
    // scalars the control loop needs are read straight out of the cache.
    auto capture = [&]() {
        pv.valid = dataValid;
        pv.timestamp = lastReadingTimestamp;
        const auto *m = Sensor::findMeasurement(currentMeasurements,
                                                Sensor::MeasurementType::Temperature);
        if (m) {
            const float *f = std::get_if<float>(&m->value);
            if (f) pv.temperature = *f;
        }
    };
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        capture();
        xSemaphoreGive(dataMutex);
    }
#else
    capture();
#endif
    return pv;
}

std::vector<Sensor::Measurement> SensorController::getValidMeasurements() const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        std::vector<Sensor::Measurement> result;
        if (dataValid) result = currentMeasurements;
        xSemaphoreGive(dataMutex);
        return result;
    }
    return {};
#else
    return dataValid ? currentMeasurements : std::vector<Sensor::Measurement>{};
#endif
}

float SensorController::getFloatMeasurement(Sensor::MeasurementType type) const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto* m = Sensor::findMeasurement(currentMeasurements, type);
        float result = NAN;
        if (m) {
            const float* f = std::get_if<float>(&m->value);
            if (f) result = *f;
        }
        xSemaphoreGive(dataMutex);
        return result;
    }
    return NAN;
#else
    auto* m = Sensor::findMeasurement(currentMeasurements, type);
    if (!m) return NAN;
    const float* f = std::get_if<float>(&m->value);
    return f ? *f : NAN;
#endif
}

int32_t SensorController::getIntMeasurement(Sensor::MeasurementType type) const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto* m = Sensor::findMeasurement(currentMeasurements, type);
        int32_t result = -1;
        if (m) {
            const int32_t* i = std::get_if<int32_t>(&m->value);
            if (i) result = *i;
        }
        xSemaphoreGive(dataMutex);
        return result;
    }
    return -1;
#else
    auto* m = Sensor::findMeasurement(currentMeasurements, type);
    if (!m) return -1;
    const int32_t* i = std::get_if<int32_t>(&m->value);
    return i ? *i : -1;
#endif
}

float SensorController::getTemperature() const {
    return getFloatMeasurement(Sensor::MeasurementType::Temperature);
}

float SensorController::getRelativeHumidity() const {
    return getFloatMeasurement(Sensor::MeasurementType::RelativeHumidity);
}

float SensorController::getDewPoint() const {
    return getFloatMeasurement(Sensor::MeasurementType::DewPoint);
}

int32_t SensorController::getVocIndex() const {
    return getIntMeasurement(Sensor::MeasurementType::VocIndex);
}

uint32_t SensorController::getLastReadingTimestamp() const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        uint32_t val = lastReadingTimestamp;
        xSemaphoreGive(dataMutex);
        return val;
    }
    return 0;
#else
    return lastReadingTimestamp;
#endif
}

bool SensorController::isDataValid() const {
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        bool val = dataValid;
        xSemaphoreGive(dataMutex);
        return val;
    }
    return false;
#else
    return dataValid;
#endif
}

Sensor::Sensor *SensorController::getSensor(size_t index) {
    if (index < sensors.size()) {
        return sensors[index].get();
    }
    return nullptr;
}

uint32_t SensorController::getTimeSinceLastReading() const {
    uint32_t readingTime;
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        readingTime = lastReadingTime;
        xSemaphoreGive(dataMutex);
    } else {
        return 0;
    }
#else
    readingTime = lastReadingTime;
#endif
    if (readingTime == 0) {
        return 0;
    }
    return millis() - readingTime;
}

bool SensorController::hasConnectedSensors() const {
    return std::any_of(sensors.begin(), sensors.end(),
        [](const auto &sensor) {
            return sensor && sensor->getStatus() == Sensor::SensorStatus::Online;
        });
}

