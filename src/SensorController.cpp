#include "SensorController.h"
#include <algorithm>
#include "SensorDataLogger.h"

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
    : config(config), targetTemperature(22.0f), controlEnabled(false), lastReadingTime(0),
      logInterval(60000), lastLogTime(0) {
    // Initialize current data to invalid state
    currentData.valid = false;
    // Initialize data logger with capacity for 1000 entries
    dataLogger = std::make_unique<SensorDataLogger>(1000);
}

void SensorController::begin() {
    // Initialize all sensors
    for (auto &sensor : sensors) {
        if (sensor && !sensor->begin()) {
            Serial.printf("SensorController: Failed to initialize sensor %s\n", sensor->getName());
        } else {
            Serial.printf("SensorController: Initialized sensor %s (%s)\n", 
                         sensor->getName(), sensor->getType());
        }
    }
    
    // Load configuration
    // TODO: Load target temperature and control settings from config
    targetTemperature = 22.0f; // Default comfortable room temperature
    controlEnabled = false; // Start with control disabled
}

void SensorController::addSensor(std::unique_ptr<Sensor::Sensor> sensor) {
    if (sensor) {
        sensors.push_back(std::move(sensor));
    }
}

void SensorController::readSensors() {
    bool anyValid = false;
    Sensor::SensorData combinedData;
    combinedData.timestamp = millis();
    
    // Read all sensors and average the results
    float tempSum = 0.0f;
    float humiditySum = 0.0f;
    int validCount = 0;
    
    for (auto &sensor : sensors) {
        if (sensor && sensor->isConnected()) {
            Sensor::SensorData data = sensor->read();
            if (data.valid) {
                tempSum += data.temperature;
                humiditySum += data.humidity;
                validCount++;
                anyValid = true;
            }
        }
    }
    
    if (anyValid && validCount > 0) {
        combinedData.temperature = tempSum / validCount;
        combinedData.humidity = humiditySum / validCount;
        combinedData.valid = true;
        currentData = combinedData;
        lastReadingTime = combinedData.timestamp;
        
        // Log the data if logging is enabled
        uint32_t now = millis();
        if (now - lastLogTime >= logInterval) {
            dataLogger->addReading(combinedData);
            lastLogTime = now;
        }
    } else {
        currentData.valid = false;
    }
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
    if (!controlEnabled || !currentData.valid) {
        return 0.0f; // No control if disabled or no valid data
    }
    
    // Simple PID controller implementation
    static float integral = 0.0f;
    static float previousError = 0.0f;
    static uint32_t lastControlTime = 0;
    
    uint32_t now = millis();
    float dt = (now - lastControlTime) / 1000.0f; // Convert to seconds
    lastControlTime = now;
    
    // Calculate error
    float error = targetTemperature - currentData.temperature;
    
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
    
    // Debug output
    if (dt > 0) { // Only log if we have a valid time delta
        Serial.printf("Control: T=%.1f°C (target=%.1f°C), output=%.2f, P=%.2f, I=%.2f, D=%.2f\n",
                     currentData.temperature, targetTemperature, output, proportional, integral, derivative);
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