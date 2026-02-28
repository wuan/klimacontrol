#include "SensorController.h"
#include <algorithm>
#include <cmath>
#include "StatusLed.h"
#include "Network.h"

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
    : config(config), network(nullptr), lastReadingTimestamp(0), dataValid(false),
      targetTemperature(22.0f), controlEnabled(false), lastReadingTime(0) {
}

void SensorController::setNetwork(Network *network) {
    this->network = network;
}

void SensorController::begin() {
    Serial.println("SensorController: Beginning sensor initialization...");

    // Initialize all sensors
    for (auto &sensor : sensors) {
        if (sensor) {
            Serial.printf("SensorController: Initializing sensor %s...\n", sensor->getName());
            if (!sensor->begin()) {
                Serial.printf("SensorController: Failed to initialize sensor %s\n", sensor->getName());
            } else {
                Serial.printf("SensorController: Successfully initialized sensor %s (%s)\n",
                             sensor->getName(), sensor->getType());
            }
        }
    }

    Serial.printf("SensorController: Found %u sensors total\n", sensors.size());

    // Load configuration
    targetTemperature = 22.0f;
    controlEnabled = false;
}

void SensorController::addSensor(std::unique_ptr<Sensor::Sensor> sensor) {
    if (sensor) {
        sensors.push_back(std::move(sensor));
    }
}

void SensorController::readSensors() {
    // Set LED to yellow during measurement
    setStatusLedMeasuring();

    // Serial.println("SensorController: Reading sensors...");
    // Serial.printf("SensorController: Found %u sensors, checking connections...\n", sensors.size());

    std::vector<Sensor::Measurement> allMeasurements;
    bool anyValid = false;
    uint32_t timestamp = millis();

    for (auto &sensor : sensors) {
        if (sensor) {
            // if (sensor->isConnected()) {
                // Serial.printf("SensorController: Reading from sensor %s...\n", sensor->getName());
                uint32_t readStart = millis();
                Sensor::SensorReading reading = sensor->read();
                uint32_t readTime = millis() - readStart;
                if (reading.valid) {
#if DEBUG
                    Serial.printf("SensorController: Sensor %s #%d (%u ms): ", sensor->getName(), reading.measurements.size(), readTime);
                    bool first = true;
#endif
                    for (const auto &m : reading.measurements) {
#if DEBUG
                        if (!first) Serial.print(", ");
                        if (auto* i = std::get_if<int32_t>(&m.value)) {
                            Serial.printf("%s: %d %s", m.type, *i, m.unit);
                        } else {
                            Serial.printf("%s: %.1f %s", m.type, std::get<float>(m.value), m.unit);
                        }
                        first = false;
#endif
                        allMeasurements.push_back(m);
                    }
#ifdef DEBUG
                    Serial.print("\n");
#endif

                    allMeasurements.push_back({"time", (int32_t)readTime, "ms", sensor->getName(), false});
                    anyValid = true;
                } else {
                    Serial.printf("SensorController: Sensor %s - invalid data\n", sensor->getName());
                }
            } else {
                Serial.printf("SensorController: Sensor %s - not connected\n", sensor->getName());
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

    // Set LED back to normal after measurement
    setStatusLedNormal();
}

float SensorController::getTemperature() const {
    for (const auto &m : currentMeasurements) {
        if (strcmp(m.type, "temperature") == 0) {
            return std::get<float>(m.value);
        }
    }
    return NAN;
}

float SensorController::getRelativeHumidity() const {
    for (const auto &m : currentMeasurements) {
        if (strcmp(m.type, "relative humidity") == 0) {
            return std::get<float>(m.value);
        }
    }
    return NAN;
}

float SensorController::getDewPoint() const {
    for (const auto &m : currentMeasurements) {
        if (strcmp(m.type, "dew point") == 0) {
            return std::get<float>(m.value);
        }
    }
    return NAN;
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
    Serial.printf("SensorController: Target temperature set to %.1f°C\n", targetTemperature);
}

void SensorController::setControlEnabled(bool enabled) {
    if (controlEnabled != enabled) {
        controlEnabled = enabled;
        Serial.printf("SensorController: Temperature control %s\n",
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
    float derivative = Kd * (error - previousError) / dt;
    previousError = error;

    // Calculate control output
    float output = proportional + integral + derivative;
    output = std::max(MinOutput, std::min(MaxOutput, output));

    if (dt > 0) {
        Serial.printf("Control: T=%.1f°C (target=%.1f°C), output=%.2f, P=%.2f, I=%.2f, D=%.2f\n",
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

void SensorController::setStatusLedMeasuring() {
    if (network) {
        network->setStatusLedState(LedState::MEASURING);
    }
}

void SensorController::setStatusLedNormal() {
    if (network) {
        network->setStatusLedState(LedState::ON);
    }
}
