#include "SensorController.h"
#include <algorithm>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#endif

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
      targetTemperature(22.0f), controlEnabled(false), lastReadingTime(0) {
}

void SensorController::begin() {
    Serial.println("SensorController: Beginning sensor initialization...");

    // Initialize all sensors
    for (auto &sensor : sensors) {
        if (sensor) {
            Serial.printf("SensorController: Initializing sensor %s...\r\n", sensor->getType());
            if (!sensor->begin()) {
                Serial.printf("SensorController: Failed to initialize sensor %s\r\n", sensor->getType());
            } else {
                Serial.printf("SensorController: Successfully initialized sensor %s\r\n", sensor->getType());
            }
        }
    }

    Serial.printf("SensorController: Found %u sensors total\r\n", sensors.size());

    sortSensors();

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
                Serial.printf("SensorController: Read order [%u] %s\r\n",
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
            Serial.printf("SensorController: WARNING: %s has unmet dependencies, appending last\r\n",
                sensors[i]->getType());
            sorted.push_back(std::move(sensors[i]));
        }
    }

    sensors = std::move(sorted);
}

void SensorController::readSensors() {
    // Serial.println("SensorController: Reading sensors...");
    // Serial.printf("SensorController: Found %u sensors, checking connections...\r\n", sensors.size());

    std::vector<Sensor::Measurement> allMeasurements;
    bool anyValid = false;
    uint32_t timestamp = millis();

    Sensor::ReadConfig readConfig;
    readConfig.elevation = config.loadDeviceConfig().elevation;

    for (auto &sensor : sensors) {
        if (sensor) {
            // if (sensor->isConnected()) {
                // Serial.printf("SensorController: Reading from sensor %s...\r\n", sensor->getType());
                uint32_t readStart = millis();
                Sensor::SensorReading reading = sensor->read(readConfig, allMeasurements);
                uint32_t readTime = millis() - readStart;
                if (reading.valid) {
#if DEBUG
                    Serial.printf("SensorController: Sensor %s #%d (%u ms): ", sensor->getType(), reading.measurements.size(), readTime);
                    bool first = true;
#endif
                    for (const auto &m : reading.measurements) {
#if DEBUG
                        if (!first) Serial.print(", ");
                        if (auto* i = std::get_if<int32_t>(&m.value)) {
                            Serial.printf("%s: %d %s", Sensor::measurementTypeLabel(m.type), *i, Sensor::measurementTypeUnit(m.type));
                        } else {
                            Serial.printf("%s: %.1f %s", Sensor::measurementTypeLabel(m.type), std::get<float>(m.value), Sensor::measurementTypeUnit(m.type));
                        }
                        first = false;
#endif
                        allMeasurements.push_back(m);
                    }
#ifdef DEBUG
                    Serial.print("\r\n");
#endif

                    allMeasurements.push_back({Sensor::MeasurementType::Time, (int32_t)readTime, sensor->getType(), false});
                    anyValid = true;
                } else {
                    Serial.printf("SensorController: Sensor %s - invalid data\r\n", sensor->getType());
                }
            } else {
                Serial.println("SensorController: Sensor - not connected (null)");
            }
        // }
    }

    if (anyValid) {
        currentMeasurements = std::move(allMeasurements);
        lastReadingTimestamp = timestamp;
        dataValid = true;
        lastReadingTime = timestamp;
    } else {
        dataValid = false;
    }

}

float SensorController::getTemperature() const {
    auto* m = Sensor::findMeasurement(currentMeasurements, Sensor::MeasurementType::Temperature);
    if (!m) return NAN;
    const float* f = std::get_if<float>(&m->value);
    return f ? *f : NAN;
}

float SensorController::getRelativeHumidity() const {
    auto* m = Sensor::findMeasurement(currentMeasurements, Sensor::MeasurementType::RelativeHumidity);
    if (!m) return NAN;
    const float* f = std::get_if<float>(&m->value);
    return f ? *f : NAN;
}

float SensorController::getDewPoint() const {
    auto* m = Sensor::findMeasurement(currentMeasurements, Sensor::MeasurementType::DewPoint);
    if (!m) return NAN;
    const float* f = std::get_if<float>(&m->value);
    return f ? *f : NAN;
}

int32_t SensorController::getVocIndex() const {
    auto* m = Sensor::findMeasurement(currentMeasurements, Sensor::MeasurementType::VocIndex);
    if (!m) return -1;
    const int32_t* i = std::get_if<int32_t>(&m->value);
    return i ? *i : -1;
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
    Serial.printf("SensorController: Target temperature set to %.1f°C\r\n", targetTemperature);
}

void SensorController::setControlEnabled(bool enabled) {
    if (controlEnabled != enabled) {
        controlEnabled = enabled;
        Serial.printf("SensorController: Temperature control %s\r\n",
                     enabled ? "enabled" : "disabled");
    }
}

float SensorController::updateControl() {
    float currentTemp = getTemperature();
    if (!controlEnabled || !dataValid || std::isnan(currentTemp)) {
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
        Serial.printf("Control: T=%.1f°C (target=%.1f°C), output=%.2f, P=%.2f, I=%.2f, D=%.2f\r\n",
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
        if (sensor && sensor->isConnected()) {
            return true;
        }
    }
    return false;
}

