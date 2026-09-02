#include "SensorController.h"
#include "sensor/DeviceSensor.h"
#include "Log.h"
#include <algorithm>
#include <cmath>
#include <numeric>

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
    // Defined in control/PidController.h so the API can report them; see the
    // note there about why they are not in an anonymous namespace any more.
    constexpr Control::PidGains PID_GAINS = Control::DEFAULT_GAINS;
    constexpr float MaxOutput = Control::DEFAULT_MAX_OUTPUT;
    constexpr float MinOutput = Control::DEFAULT_MIN_OUTPUT;

    // Upper bound on measurements per sensor in a single read cycle. Used
    // to pre-reserve `currentMeasurements` capacity at boot so the per-cycle
    // assignment does not reallocate. Generous (8) so a sensor that adds
    // extra derived measurements (dew point, sea-level pressure, etc.) still
    // fits without growing the vector. See spec `memory-management` →
    // "Vector capacities are reserved at boot" for the contract.
    constexpr size_t MAX_MEASUREMENTS_PER_SENSOR = 8;
}

SensorController::SensorController(Config::ConfigManager &config, [[maybe_unused]] StatusLed *statusLed)
    : config(config), lastReadingTimestamp(0), dataValid(false),
#ifdef ARDUINO
      dataMutex(xSemaphoreCreateMutex()),
      statusLed(statusLed),
#endif
      lastReadingTime(0),
      lastControlOutput(0.0f),
      pid(PID_GAINS, MinOutput, MaxOutput),
      autotuner(Control::AutotuneLimits{}) {
#ifdef ARDUINO
    // xSemaphoreCreateMutex() returns nullptr if the heap is exhausted at boot.
    // Previously this just logged a warning and continued, which let the
    // device appear healthy while producing no real sensor data (silent
    // degradation). Now we fail hard: drive the LED to ERROR, hold briefly so
    // a human can see it, then restart.
    if (!dataMutex) {
        ESP_LOGE(TAG, "Failed to create dataMutex (out of memory) — restarting");
        if (statusLed) {
            statusLed->setState(LedState::ERROR);
            statusLed->update();
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
    uint32_t timestamp = millis();
    std::vector<Sensor::Measurement> allMeasurements;
    bool anyValid = false;
#ifdef ARDUINO
    bool anyI2CSensor = false;   // at least one I2C sensor is configured
    bool anyI2CValid = false;    // at least one I2C sensor read valid this cycle
#endif

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

        // Pre-reserve: each sensor contributes measurementCount() data measurements
        // plus 1 Time measurement added per valid sensor by this function
        size_t totalExpected = std::accumulate(sensors.begin(), sensors.end(), size_t(0),
            [](size_t sum, const auto &sensor) {
                return sum + (sensor ? sensor->measurementCount() + 1 : 0);
            });
        allMeasurements.reserve(totalExpected);

        for (auto &sensor : sensors) {
            if (!sensor) continue;

#ifdef ARDUINO
            if (sensor->usesI2C()) anyI2CSensor = true;
#endif

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
                    allMeasurements.push_back(m);
                }

                allMeasurements.push_back({Sensor::MeasurementType::Time, (int32_t)readTime, sensor->getType(), false});
                anyValid = true;
#ifdef ARDUINO
                if (sensor->usesI2C()) anyI2CValid = true;
#endif
            } else {
                ESP_LOGW(TAG, "Sensor %s - invalid data", sensor->getType());
            }
        }

#ifdef ARDUINO
        // I2C bus recovery: if I2C sensors are configured but none produced a
        // valid reading this cycle, the bus may be wedged (a slave stuck holding
        // SDA low). After a short streak, attempt recovery while we still hold the
        // bus lock so no scan can interleave. The DeviceSensor is not I2C, so it
        // never masks this condition.
        if (anyI2CSensor && !anyI2CValid) {
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

    // ===== PHASE 2: Data update (I2C bus NOT locked) =====
#ifdef ARDUINO
    if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
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

void SensorController::setTargetTemperature(float temperature) {
    // Last line of defence for callers that do not come through HTTP — chiefly
    // main.cpp restoring the persisted value at boot. Requests arriving via
    // POST /api/temperature/target are range-checked and *rejected* by the
    // route handler before they get here, because silently substituting a
    // different setpoint than the one asked for is not an acceptable answer to
    // a user. See spec `temperature-control` → "Setpoint range" for the three
    // validation layers and what each is for.
    float clamped = std::max(Config::TARGET_TEMPERATURE_MIN_C,
                             std::min(Config::TARGET_TEMPERATURE_MAX_C, temperature));
    ESP_LOGI(TAG, "Target temperature set to %.1f C", clamped);

    // Persist to NVS using partial update (also updates in-memory cache)
    config.updateTargetTemperature(clamped);
}

void SensorController::setControlEnabled(bool enabled) {
    // Configuration write only. This runs on the web-server task, while the PID
    // accumulators are owned by the Sensor Monitor task; resetting them from
    // here would race a control tick that is mid read-modify-write and could
    // silently lose the reset. updateControl() detects the resumption itself.
    if (config.getDeviceConfig().temperature_control_enabled != enabled) {
        ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");

        // Persist to NVS using partial update (also updates in-memory cache)
        config.updateTemperatureControlEnabled(enabled);
    }
}

bool SensorController::requestAutotuneStart() {
    // Early refusal on the web task so the API can answer immediately. The
    // control loop re-checks both conditions, because it is authoritative and
    // the state can move between here and the next tick.
    if (!config.getDeviceConfig().temperature_control_enabled) {
        return false;
    }
    if (isAutotuneActive()) {
        return false;
    }
    autotuneStartRequested.store(true);
    return true;
}

void SensorController::requestAutotuneCancel() {
    autotuneCancelRequested.store(true);
}

bool SensorController::acceptAutotuneResult() {
    if (autotuner.state() != Control::AutotuneState::Done) {
        return false;
    }
    // In memory only: DeviceConfig has no gain fields, so a restart returns the
    // compiled-in defaults. setGains() suspends, so the next tick restarts
    // bumplessly rather than carrying an integral accumulated under the old
    // gains into the new ones.
    pid.setGains(autotuner.getResultGains());
    ESP_LOGI(TAG, "Autotune gains applied (not persisted): Kp=%.4f Ki=%.5f Kd=%.1f",
             static_cast<double>(autotuner.result().gains.kp),
             static_cast<double>(autotuner.result().gains.ki),
             static_cast<double>(autotuner.result().gains.kd));
    return true;
}

float SensorController::updateControl() {
    const uint32_t now = millis();

    // Cross-task requests are consumed here, on the only task allowed to mutate
    // autotuner state. Cancel first, so a cancel and a start arriving in the
    // same tick mean "stop the old run, begin a new one" rather than resolving
    // by arrival order.
    if (autotuneCancelRequested.exchange(false)) {
        autotuner.cancel();
    }
    if (autotuneStartRequested.exchange(false)) {
        if (config.getDeviceConfig().temperature_control_enabled && !isAutotuneActive()) {
            ESP_LOGI(TAG, "Autotune starting around %.1f C",
                     static_cast<double>(config.getDeviceConfig().target_temperature));
            autotuner.start(config.getDeviceConfig().target_temperature, now);
        }
    }

    if (isAutotuneActive()) {
        // A run switched off underneath itself would be driving an output
        // nobody enabled.
        if (!config.getDeviceConfig().temperature_control_enabled) {
            autotuner.cancel();
            pid.suspend();
            lastControlOutput = 0.0f;
            return 0.0f;
        }

        // The autotuner owns the output for the duration. Suspending the PID is
        // what makes the handover back correct: the run is exactly the kind of
        // gap the bumpless-restart path was built for.
        pid.suspend();
        const float currentTemp = getTemperature();
        const bool valid = isDataValid() && !std::isnan(currentTemp);
        lastControlOutput = autotuner.update(currentTemp, valid, now);

        if (!isAutotuneActive()) {
            ESP_LOGI(TAG, "Autotune finished: state=%d reason=%d cycles=%u",
                     static_cast<int>(autotuner.state()),
                     static_cast<int>(autotuner.abortReason()),
                     static_cast<unsigned>(autotuner.completedCycles()));
        }
        return lastControlOutput;
    }

    float currentTemp = getTemperature();
    if (!config.getDeviceConfig().temperature_control_enabled || !isDataValid() ||
        std::isnan(currentTemp)) {
        // Tell the PID it skipped a tick, so the next one that does run
        // restarts bumplessly instead of charging its integral with the whole
        // elapsed gap. All three of the disabled, no-valid-data and NaN cases
        // land here, and all three leave the same stale-timestamp trap.
        pid.suspend();

        // The stored output must reflect reality on every call, otherwise
        // isControlActive() keeps reporting the last positive output after the
        // sensor drops out or control is switched off.
        lastControlOutput = 0.0f;
        return 0.0f;
    }

    const bool restarting = !pid.isRunning();

    float targetTemperature = config.getDeviceConfig().target_temperature;
    float error = targetTemperature - currentTemp;
    float output = pid.update(error, now);

    lastControlOutput = output;

    if (restarting) {
        ESP_LOGD(TAG, "PID restart: T=%.1f C (target=%.1f C), output=%.2f (proportional only)",
                 currentTemp, targetTemperature, output);
    } else {
        ESP_LOGD(TAG, "PID: T=%.1f C (target=%.1f C), output=%.2f, I=%.2f", currentTemp,
                 targetTemperature, output, pid.getIntegral());
    }

    return output;
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

