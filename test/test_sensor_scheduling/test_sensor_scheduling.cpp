#include "unity.h"
#include <cmath>
#include <vector>

#include "Config.h"
#include "SensorController.h"
#include "sensor/Sensor.h"
#include "sensor/SGP40.h"

void setUp() {}
void tearDown() {}

// These tests drive the real SensorController::readSensors(uint32_t nowMs)
// with an explicit clock, so they assert on cadence directly: which sensors
// are read on which tick, what the snapshot contains between reads, and when
// cached readings are dropped. See the `sensor-management` spec, requirements
// "Sensors are read only when due", "Per-sensor last-good cache" and
// "Dependent sensors receive cached inputs".

namespace {

    // Small on purpose. Sensor::tryBegin() stamps lastInitAttempt from the real
    // millis(), and readSensors() retries InitFailed/ReadFailing sensors once
    // nowMs is 30 s past that stamp. Keeping the injected clock under 30 s
    // for the short tests means a deliberately failed mock stays failed.
    constexpr uint32_t T0 = 5000;
    constexpr uint32_t TICK = 1000;
    constexpr uint32_t INTERVAL = SensorController::MEASUREMENT_INTERVAL_MS;

    // Inside a class derived from Sensor::Sensor the injected class name
    // shadows the namespace, so the mock spells the namespace through an alias.
    namespace S = ::Sensor;

    // A scriptable sensor. `interval` is what requiredIntervalMs() returns;
    // `provides` / `needs` feed the controller's dependency sort; `nextValid`
    // decides what the next read() returns; `reads` counts read() calls and
    // `lastPrior` captures the `prior` vector of the most recent call.
    class MockSensor : public S::Sensor {
    public:
        const char *name;
        uint32_t interval;
        std::vector<S::MeasurementType> provides;
        std::vector<S::MeasurementType> needs;
        bool nextValid = true;
        float temperature = 21.0f;
        float humidity = 45.0f;
        int reads = 0;
        std::vector<S::Measurement> lastPrior;

        MockSensor(const char *name, uint32_t interval,
                   std::vector<S::MeasurementType> provides,
                   std::vector<S::MeasurementType> needs = {})
            : name(name), interval(interval), provides(std::move(provides)),
              needs(std::move(needs)) {}

        bool begin() override { return true; }
        const char *getType() const override { return name; }
        uint32_t requiredIntervalMs() const override { return interval; }
        S::TypeSpan providesMeasurements() const override {
            return {provides.data(), static_cast<uint8_t>(provides.size())};
        }
        S::TypeSpan requiresMeasurements() const override {
            return {needs.data(), static_cast<uint8_t>(needs.size())};
        }

        S::SensorReading read(const S::ReadConfig &,
                              const std::vector<S::Measurement> &prior) override {
            ++reads;
            lastPrior = prior;
            S::SensorReading r;
            r.valid = nextValid;
            if (!nextValid) return r;
            for (auto type : provides) {
                switch (type) {
                    case S::MeasurementType::Temperature:
                        r.measurements.push_back({type, temperature, name, false});
                        break;
                    case S::MeasurementType::RelativeHumidity:
                        r.measurements.push_back({type, humidity, name, false});
                        break;
                    default:
                        r.measurements.push_back({type, static_cast<int32_t>(100), name, false});
                        break;
                }
            }
            return r;
        }
    };

    // Owns the config and controller, adds mocks, and brings them Online
    // without going through begin() (which would append a DeviceSensor and
    // touch the PID). Keeps raw pointers to the mocks for assertions.
    struct Rig {
        Config::ConfigManager config;
        SensorController controller{config, nullptr};
        std::vector<MockSensor *> mocks;

        MockSensor *add(const char *name, uint32_t interval,
                        std::vector<Sensor::MeasurementType> provides,
                        std::vector<Sensor::MeasurementType> needs = {}) {
            auto sensor = std::make_unique<MockSensor>(name, interval, std::move(provides),
                                                       std::move(needs));
            MockSensor *raw = sensor.get();
            raw->tryBegin();
            controller.addSensor(std::move(sensor));
            mocks.push_back(raw);
            return raw;
        }
    };

    bool hasType(const std::vector<Sensor::Measurement> &ms, Sensor::MeasurementType type,
                 const char *sensor = nullptr) {
        for (const auto &m : ms) {
            if (m.type == type && (!sensor || m.sensor == sensor)) return true;
        }
        return false;
    }

    // Convenience: an SHT4x-like default sensor and an SGP40-like 1 Hz sensor.
    using MT = Sensor::MeasurementType;
    const std::vector<MT> TEMP_RH = {MT::Temperature, MT::RelativeHumidity};
    const std::vector<MT> VOC = {MT::VocIndex};
}

// --- Requirement: Sensors declare their required read interval ---

void test_base_sensor_has_no_interval_requirement() {
    MockSensor s("x", 0, TEMP_RH);
    const Sensor::Sensor &base = s;
    TEST_ASSERT_EQUAL_UINT32(0, base.requiredIntervalMs());
}

void test_sgp40_requires_one_hertz() {
    Sensor::SGP40 sgp;
    TEST_ASSERT_EQUAL_UINT32(1000, sgp.requiredIntervalMs());
}

void test_measurement_interval_constant() {
    TEST_ASSERT_EQUAL_UINT32(15000, SensorController::MEASUREMENT_INTERVAL_MS);
}

// --- Requirement: Sensors are read only when due ---

void test_first_cycle_reads_everything() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);
    auto *sgp = rig.add("SGP", 1000, VOC, TEMP_RH);

    rig.controller.readSensors(T0);

    TEST_ASSERT_EQUAL(1, sht->reads);
    TEST_ASSERT_EQUAL(1, sgp->reads);
}

void test_default_sensor_skipped_inside_interval() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);

    rig.controller.readSensors(T0);
    rig.controller.readSensors(T0 + TICK);

    TEST_ASSERT_EQUAL(1, sht->reads);
}

void test_required_interval_sensor_read_every_tick() {
    Rig rig;
    auto *sgp = rig.add("SGP", 1000, VOC);

    rig.controller.readSensors(T0);
    rig.controller.readSensors(T0 + TICK);
    rig.controller.readSensors(T0 + 2 * TICK);

    TEST_ASSERT_EQUAL(3, sgp->reads);
}

void test_default_sensors_share_a_phase() {
    Rig rig;
    auto *a = rig.add("A", 0, TEMP_RH);
    auto *b = rig.add("B", 0, {MT::Pressure});

    rig.controller.readSensors(T0);
    for (uint32_t t = TICK; t < INTERVAL; t += TICK) {
        rig.controller.readSensors(T0 + t);
    }
    TEST_ASSERT_EQUAL(1, a->reads);
    TEST_ASSERT_EQUAL(1, b->reads);

    rig.controller.readSensors(T0 + INTERVAL);
    TEST_ASSERT_EQUAL(2, a->reads);
    TEST_ASSERT_EQUAL(2, b->reads);
}

void test_late_tick_shifts_phase_without_double_read() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);

    rig.controller.readSensors(T0);
    rig.controller.readSensors(T0 + 16000);
    TEST_ASSERT_EQUAL(2, sht->reads);

    // Next default read is due at T0 + 31000, not T0 + 30000.
    rig.controller.readSensors(T0 + 30000);
    TEST_ASSERT_EQUAL(2, sht->reads);
    rig.controller.readSensors(T0 + 31000);
    TEST_ASSERT_EQUAL(3, sht->reads);
}

void test_late_online_sensor_joins_shared_phase() {
    // A sensor that only comes Online after the first cycle is read on the
    // next shared phase, together with the others, not on its own timer.
    Rig rig;
    auto *a = rig.add("A", 0, TEMP_RH);
    auto late = std::make_unique<MockSensor>("B", 0, std::vector<MT>{MT::Pressure});
    MockSensor *b = late.get();
    rig.controller.addSensor(std::move(late)); // Uninitialized: not read

    rig.controller.readSensors(T0);
    TEST_ASSERT_EQUAL(1, a->reads);
    TEST_ASSERT_EQUAL(0, b->reads);

    b->tryBegin();
    rig.controller.readSensors(T0 + 5000);
    TEST_ASSERT_EQUAL(0, b->reads); // waits for the shared phase

    rig.controller.readSensors(T0 + INTERVAL);
    TEST_ASSERT_EQUAL(2, a->reads);
    TEST_ASSERT_EQUAL(1, b->reads);
}

// --- Requirement: Per-sensor last-good cache ---

void test_measurements_persist_across_skipped_tick() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);
    sht->temperature = 22.5f;

    rig.controller.readSensors(T0);
    rig.controller.readSensors(T0 + TICK);

    TEST_ASSERT_EQUAL(1, sht->reads);
    TEST_ASSERT_TRUE(rig.controller.isDataValid());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.5f, rig.controller.getTemperature());
    auto ms = rig.controller.getMeasurements();
    TEST_ASSERT_TRUE(hasType(ms, MT::Temperature, "SHT"));
    TEST_ASSERT_TRUE(hasType(ms, MT::RelativeHumidity, "SHT"));
    TEST_ASSERT_TRUE(hasType(ms, MT::Time, "SHT"));
}

void test_failed_read_keeps_last_good_while_online() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);
    sht->temperature = 22.5f;

    rig.controller.readSensors(T0);
    sht->nextValid = false;
    rig.controller.readSensors(T0 + INTERVAL);

    TEST_ASSERT_EQUAL(2, sht->reads);
    TEST_ASSERT_EQUAL(Sensor::SensorStatus::Online, sht->getStatus());
    TEST_ASSERT_TRUE(rig.controller.isDataValid());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.5f, rig.controller.getTemperature());
}

void test_slot_cleared_when_sensor_leaves_online() {
    // The status transition alone must clear the slot, independent of age.
    // Ten failed reads at 15 s spacing would take 150 s and trip the 45 s
    // expiry first, so drive the status directly via recordReadResult() and
    // tick only 1 s later.
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);

    rig.controller.readSensors(T0);
    TEST_ASSERT_TRUE(rig.controller.isDataValid());

    for (int i = 0; i < 10; ++i) sht->recordReadResult(false);
    TEST_ASSERT_EQUAL(Sensor::SensorStatus::ReadFailing, sht->getStatus());

    rig.controller.readSensors(T0 + TICK);
    TEST_ASSERT_FALSE(rig.controller.isDataValid());
    TEST_ASSERT_EQUAL(0, rig.controller.getMeasurements().size());
    TEST_ASSERT_TRUE(std::isnan(rig.controller.getTemperature()));
}

void test_slot_expires_after_three_missed_intervals() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);

    rig.controller.readSensors(T0);
    sht->nextValid = false;

    // Still valid at exactly 3 intervals...
    rig.controller.readSensors(T0 + 3 * INTERVAL);
    TEST_ASSERT_TRUE(rig.controller.isDataValid());

    // ...dropped one millisecond later.
    rig.controller.readSensors(T0 + 3 * INTERVAL + 1);
    TEST_ASSERT_FALSE(rig.controller.isDataValid());
    TEST_ASSERT_EQUAL(0, rig.controller.getValidMeasurements().size());
    TEST_ASSERT_EQUAL(0, rig.controller.getMeasurements().size());
}

void test_required_interval_sensor_expiry_uses_its_own_interval() {
    Rig rig;
    auto *sgp = rig.add("SGP", 1000, VOC);

    rig.controller.readSensors(T0);
    sgp->nextValid = false;
    rig.controller.readSensors(T0 + 3000);
    TEST_ASSERT_TRUE(rig.controller.isDataValid());
    rig.controller.readSensors(T0 + 3001);
    TEST_ASSERT_FALSE(rig.controller.isDataValid());
}

void test_snapshot_order_follows_sensor_order() {
    Rig rig;
    auto *first = rig.add("FIRST", 0, TEMP_RH);
    auto *second = rig.add("SECOND", 0, TEMP_RH);
    first->temperature = 20.0f;
    second->temperature = 25.0f;

    rig.controller.readSensors(T0);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, rig.controller.getTemperature());

    // Only the second one is re-read (first fails) — order must still hold.
    first->nextValid = false;
    rig.controller.readSensors(T0 + INTERVAL);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, rig.controller.getTemperature());
}

void test_snapshot_timestamp_advances_only_on_a_successful_read() {
    Rig rig;
    rig.add("SHT", 0, TEMP_RH);

    rig.controller.readSensors(T0);
    TEST_ASSERT_EQUAL_UINT32(T0, rig.controller.getLastReadingTimestamp());

    rig.controller.readSensors(T0 + TICK); // nothing due
    TEST_ASSERT_EQUAL_UINT32(T0, rig.controller.getLastReadingTimestamp());

    rig.controller.readSensors(T0 + INTERVAL);
    TEST_ASSERT_EQUAL_UINT32(T0 + INTERVAL, rig.controller.getLastReadingTimestamp());
}

// --- Requirement: Dependent sensors receive cached inputs ---

void test_dependent_sees_cached_inputs_between_provider_reads() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);
    auto *sgp = rig.add("SGP", 1000, VOC, TEMP_RH);
    sht->temperature = 23.0f;

    rig.controller.readSensors(T0);
    rig.controller.readSensors(T0 + TICK);

    TEST_ASSERT_EQUAL(1, sht->reads);
    TEST_ASSERT_EQUAL(2, sgp->reads);
    TEST_ASSERT_TRUE(hasType(sgp->lastPrior, MT::Temperature, "SHT"));
    TEST_ASSERT_TRUE(hasType(sgp->lastPrior, MT::RelativeHumidity, "SHT"));
    const auto *t = Sensor::findMeasurement(sgp->lastPrior, MT::Temperature);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.0f, std::get<float>(t->value));
}

void test_same_tick_provider_is_seen_fresh() {
    Rig rig;
    auto *sht = rig.add("SHT", 0, TEMP_RH);
    auto *sgp = rig.add("SGP", 1000, VOC, TEMP_RH);

    sht->temperature = 20.0f;
    rig.controller.readSensors(T0);

    sht->temperature = 26.0f;
    rig.controller.readSensors(T0 + INTERVAL); // both due, SHT first
    const auto *t = Sensor::findMeasurement(sgp->lastPrior, MT::Temperature);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.0f, std::get<float>(t->value));
}

void test_prior_has_no_inputs_before_provider_ever_read() {
    // Provider not Online yet: dependent reads with empty prior, as before.
    Rig rig;
    auto provider = std::make_unique<MockSensor>("SHT", 0, TEMP_RH);
    rig.controller.addSensor(std::move(provider)); // never tryBegin'd
    auto *sgp = rig.add("SGP", 1000, VOC, TEMP_RH);

    rig.controller.readSensors(T0);
    TEST_ASSERT_EQUAL(1, sgp->reads);
    TEST_ASSERT_FALSE(hasType(sgp->lastPrior, MT::Temperature));
}

// --- Capacity contract (memory-management spec) ---

void test_union_does_not_reallocate_after_reserve() {
    Rig rig;
    rig.controller.reserveSensorSlots(2);
    const size_t capacity = rig.controller.getMeasurementsCapacity();
    rig.add("A", 0, TEMP_RH);
    rig.add("B", 1000, VOC, TEMP_RH);

    for (uint32_t t = 0; t <= 2 * INTERVAL; t += TICK) {
        rig.controller.readSensors(T0 + t);
    }
    TEST_ASSERT_EQUAL(capacity, rig.controller.getMeasurementsCapacity());
}

// --- Requirement: Sensor Monitor tick follows the fastest configured sensor ---

void test_min_read_interval_default_sensors_only() {
    Rig rig;
    rig.add("SHT", 0, TEMP_RH);
    rig.add("BMP", 0, {MT::Pressure});
    TEST_ASSERT_EQUAL_UINT32(INTERVAL, rig.controller.minReadIntervalMs());
}

void test_min_read_interval_one_hertz_sensor_wins() {
    Rig rig;
    rig.add("SHT", 0, TEMP_RH);
    rig.add("SGP", 1000, VOC, TEMP_RH);
    TEST_ASSERT_EQUAL_UINT32(1000, rig.controller.minReadIntervalMs());
}

void test_min_read_interval_counts_offline_sensors() {
    // A sensor that failed init is retried inside readSensors() and must find
    // the tick already running at its rate when it comes online.
    Rig rig;
    rig.add("SHT", 0, TEMP_RH);
    auto *sgp = rig.add("SGP", 1000, VOC);
    for (int i = 0; i < 10; ++i) sgp->recordReadResult(false);
    TEST_ASSERT_EQUAL(Sensor::SensorStatus::ReadFailing, sgp->getStatus());
    TEST_ASSERT_EQUAL_UINT32(1000, rig.controller.minReadIntervalMs());
}

void test_min_read_interval_no_sensors() {
    Rig rig;
    TEST_ASSERT_EQUAL_UINT32(INTERVAL, rig.controller.minReadIntervalMs());
}

int runUnityTests() {
    UNITY_BEGIN();
    RUN_TEST(test_base_sensor_has_no_interval_requirement);
    RUN_TEST(test_sgp40_requires_one_hertz);
    RUN_TEST(test_measurement_interval_constant);
    RUN_TEST(test_first_cycle_reads_everything);
    RUN_TEST(test_default_sensor_skipped_inside_interval);
    RUN_TEST(test_required_interval_sensor_read_every_tick);
    RUN_TEST(test_default_sensors_share_a_phase);
    RUN_TEST(test_late_tick_shifts_phase_without_double_read);
    RUN_TEST(test_late_online_sensor_joins_shared_phase);
    RUN_TEST(test_measurements_persist_across_skipped_tick);
    RUN_TEST(test_failed_read_keeps_last_good_while_online);
    RUN_TEST(test_slot_cleared_when_sensor_leaves_online);
    RUN_TEST(test_slot_expires_after_three_missed_intervals);
    RUN_TEST(test_required_interval_sensor_expiry_uses_its_own_interval);
    RUN_TEST(test_snapshot_order_follows_sensor_order);
    RUN_TEST(test_snapshot_timestamp_advances_only_on_a_successful_read);
    RUN_TEST(test_dependent_sees_cached_inputs_between_provider_reads);
    RUN_TEST(test_same_tick_provider_is_seen_fresh);
    RUN_TEST(test_prior_has_no_inputs_before_provider_ever_read);
    RUN_TEST(test_union_does_not_reallocate_after_reserve);
    RUN_TEST(test_min_read_interval_default_sensors_only);
    RUN_TEST(test_min_read_interval_one_hertz_sensor_wins);
    RUN_TEST(test_min_read_interval_counts_offline_sensors);
    RUN_TEST(test_min_read_interval_no_sensors);
    return UNITY_END();
}

int main() {
    return runUnityTests();
}
