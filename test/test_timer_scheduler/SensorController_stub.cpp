#include "SensorController.h"

// Minimal stub – only the constructor is needed; all other SensorController
// methods referenced by TimerScheduler are inside #ifdef ARDUINO blocks.
SensorController::SensorController(Config::ConfigManager &config)
    : config(config), lastReadingTimestamp(0), dataValid(false),
      targetTemperature(22.0f), controlEnabled(false), lastReadingTime(0) {
}

void SensorController::begin() {}
void SensorController::addSensor(std::unique_ptr<Sensor::Sensor>) {}
void SensorController::readSensors() {}
float SensorController::getTemperature() const { return 0.0f; }
float SensorController::getRelativeHumidity() const { return 0.0f; }
float SensorController::getDewPoint() const { return 0.0f; }
int32_t SensorController::getVocIndex() const { return -1; }
Sensor::Sensor *SensorController::getSensor(size_t) { return nullptr; }
void SensorController::setTargetTemperature(float temperature) {
    targetTemperature = temperature;
}
void SensorController::setControlEnabled(bool enabled) {
    controlEnabled = enabled;
}
float SensorController::updateControl() { return 0.0f; }
uint32_t SensorController::getTimeSinceLastReading() const { return 0; }
bool SensorController::hasConnectedSensors() const { return false; }
void SensorController::sortSensors() {}
