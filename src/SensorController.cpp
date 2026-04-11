#include "SensorController.h"
#include "sensor/DeviceSensor.h"
#include <algorithm>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#include <freertos/semphr.h>
#include "Log.h"
#endif

static const char* TAG = "sensor";

namespace {
    // Simple PID controller parameters (will be configurable later)
    constexpr float Kp = 2.0f;    // Proportional gain
    constexpr float Ki = 0.1f;   // Integral gain
    constexpr float Kd = 0.5f;   // Derivative gain
    constexpr float MaxOutput = 1.0f; // Maximum control output
    constexpr float MinOutput = 0.0f; // Minimum control output
}

SensorController::SensorController(Config::ConfigManager &config)
    : config(config), lastReadingTimestamp(0), dataValid(false),
#ifdef ARDUINO
      dataMutex(xSemaphoreCreateMutex()),
#endif
      targetTemperature(22.0f), controlEnabled(false), lastReadingTime(0) {
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

    // Load configuration
    targetTemperature = 22.0f;
    controlEnabled = false;
}

void SensorController::addSensor(std::unique_ptr<Sensor::Sensor> sensor) {
    if (sensor) {
        sensors.push_back(std::move(sensor));
    }
}

void SensorController::sortSensors() {
    // Topological sort: repeatedly pick sensors whose requires() are satisfied
    // by already-placed sensors' provides(). Simple quadratic — only 3-5 sensors.
    std::vector<std::unique_ptr<Sensor::Sensor>> sorted;
    sorted.reserve(sensors.size());

    std::vector<bool> placed(sensors.size(), false);

    for (size_t round = 0; round < sensors.size(); ++round) {
        bool progress = false;
        for (size_t i = 0; i < sensors.size(); ++i) {
            if (placed[i]) continue;

            Sensor::TypeSpan reqs = sensors[i]->requires();
            bool satisfied = true;

            for (uint8_t r = 0; r < reqs.count && satisfied; ++r) {
                bool found = false;
                for (auto& s : sorted) {
                    Sensor::TypeSpan prov = s->provides();
                    for (uint8_t p = 0; p < prov.count; ++p) {
                        if (prov.data[p] == reqs.data[r]) { found = true; break; }
                    }
                    if (found) break;
                }
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
    uint32_t timestamp = millis();

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

    std::vector<Sensor::Measurement> allMeasurements;
    bool anyValid = false;

    Sensor::ReadConfig readConfig;
    readConfig.elevation = config.loadDeviceConfig().elevation;

    // Pre-reserve: each sensor contributes measurementCount() data measurements
    // plus 1 Time measurement added per valid sensor by this function
    size_t totalExpected = 0;
    for (const auto &sensor : sensors) {
        if (sensor) {
            totalExpected += sensor->measurementCount() + 1;
        }
    }
    allMeasurements.reserve(totalExpected);

    for (auto &sensor : sensors) {
        if (!sensor) continue;

        // Only read sensors that are online
        if (sensor->getStatus() != Sensor::SensorStatus::Online) {
            continue;
        }

        uint32_t readStart = millis();
        Sensor::SensorReading reading = sensor->read(readConfig, allMeasurements);
        uint32_t readTime = millis() - readStart;

        sensor->recordReadResult(reading.valid);

        if (reading.valid) {
            for (const auto &m : reading.measurements) {
                if (auto* i = std::get_if<int32_t>(&m.value)) {
                    ESP_LOGD(TAG, "%s: %s=%d %s (%u ms)",
                             sensor->getType(), Sensor::measurementTypeLabel(m.type),
                             *i, Sensor::measurementTypeUnit(m.type), readTime);
                } else {
                    ESP_LOGD(TAG, "%s: %s=%.1f %s (%u ms)",
                             sensor->getType(), Sensor::measurementTypeLabel(m.type),
                             std::get<float>(m.value), Sensor::measurementTypeUnit(m.type), readTime);
                }
                allMeasurements.push_back(m);
            }

            allMeasurements.push_back({Sensor::MeasurementType::Time, (int32_t)readTime, sensor->getType(), false});
            anyValid = true;
        } else {
            ESP_LOGW(TAG, "Sensor %s - invalid data", sensor->getType());
        }
    }

#ifdef ARDUINO
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
#endif
        if (anyValid) {
            currentMeasurements = std::move(allMeasurements);
            lastReadingTimestamp = timestamp;
            dataValid = true;
            lastReadingTime = timestamp;
        } else {
            dataValid = false;
        }
#ifdef ARDUINO
        xSemaphoreGive(dataMutex);
    }
#endif

}

std::vector<Sensor::Measurement> SensorController::getMeasurements() const {
#ifdef ARDUINO
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        auto copy = currentMeasurements;
        xSemaphoreGive(dataMutex);
        return copy;
    }
    return {};
#else
    return currentMeasurements;
#endif
}

float SensorController::getFloatMeasurement(Sensor::MeasurementType type) const {
#ifdef ARDUINO
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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

void SensorController::setTargetTemperature(float temperature) {
    // Clamp to reasonable range for room temperature control
    targetTemperature = std::max(10.0f, std::min(30.0f, temperature));
    ESP_LOGI(TAG, "Target temperature set to %.1f C", targetTemperature);
}

void SensorController::setControlEnabled(bool enabled) {
    if (controlEnabled != enabled) {
        controlEnabled = enabled;
        ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");
    }
}

float SensorController::updateControl() {
    float currentTemp = getTemperature();
    if (!controlEnabled || !isDataValid() || std::isnan(currentTemp)) {
        return 0.0f;
    }

    // Simple PID controller implementation
    static float integral = 0.0f;
    static float previousError = 0.0f;
    static uint32_t lastControlTime = 0;

    uint32_t now = millis();
    float dt = (now - lastControlTime) / 1000.0f;
    lastControlTime = now;

    float error = targetTemperature - currentTemp;

    // Proportional term
    float proportional = Kp * error;

    // Integral term (with anti-windup)
    integral += Ki * error * dt;
    integral = std::max(MinOutput, std::min(MaxOutput, integral));

    // Derivative term
    float derivative = 0.0f;
    if (dt > 0.0f) {
        derivative = Kd * (error - previousError) / dt;
    }
    previousError = error;

    // Calculate control output
    float output = proportional + integral + derivative;
    output = std::max(MinOutput, std::min(MaxOutput, output));

    if (dt > 0.0f) {
        ESP_LOGD(TAG, "PID: T=%.1f C (target=%.1f C), output=%.2f, P=%.2f, I=%.2f, D=%.2f",
                 currentTemp, targetTemperature, output, proportional, integral, derivative);
    }

    return output;
}

uint32_t SensorController::getTimeSinceLastReading() const {
    if (lastReadingTime == 0) {
        return 0;
    }
    return millis() - lastReadingTime;
}

bool SensorController::hasConnectedSensors() const {
    for (const auto &sensor : sensors) {
        if (sensor && sensor->getStatus() == Sensor::SensorStatus::Online) {
            return true;
        }
    }
    return false;
}

