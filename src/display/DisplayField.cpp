#include "display/DisplayField.h"

#include <cstdio>
#include <cstring>

namespace Display {

    namespace {
        // One entry per value the panel can render. `None` is deliberately
        // absent: it selects the two-line layout and has no measurement.
        constexpr FieldSpec FIELD_SPECS[] = {
            {DisplayField::Humidity, Sensor::MeasurementType::RelativeHumidity, "", "%rH", 0, 1.0f, false},
            {DisplayField::DewPoint, Sensor::MeasurementType::DewPoint, "DP", "", 1, 0.1f, true},
            {DisplayField::Pressure, Sensor::MeasurementType::Pressure, "P", "hPa", 0, 0.5f, false},
            {DisplayField::SeaLevelPressure, Sensor::MeasurementType::SeaLevelPressure, "", "hPa", 0, 0.5f, false},
            {DisplayField::CO2, Sensor::MeasurementType::CO2, "CO2", "ppm", 0, 10.0f, false},
            {DisplayField::Illuminance, Sensor::MeasurementType::Illuminance, "", "lux", 0, 5.0f, false},
            {DisplayField::VocIndex, Sensor::MeasurementType::VocIndex, "VOC", "", 0, 5.0f, false},
            {DisplayField::PM25, Sensor::MeasurementType::PM25Concentration, "PM2.5", "ug/m3", 0, 1.0f, false},
        };

        constexpr size_t FIELD_SPEC_COUNT = sizeof(FIELD_SPECS) / sizeof(FIELD_SPECS[0]);

        // VOC index is only emitted by the SGP40 when both compensation inputs
        // were available; require them explicitly so a stale index cannot be
        // shown as a current reading.
        bool hasFiniteMeasurement(const std::vector<Sensor::Measurement>& measurements, Sensor::MeasurementType type) {
            const Sensor::Measurement* m = Sensor::findMeasurement(measurements, type);
            if (m == nullptr) {
                return false;
            }
            float value = 0.0f;
            return measurementValueAsFloat(*m, value) && std::isfinite(value);
        }

        int writeNumber(char* out, size_t n, const char* number) {
            const int written = snprintf(out, n, "%s", number);
            return written < 0 ? 0 : written;
        }
    } // namespace

    const FieldSpec* fieldSpec(DisplayField field) {
        for (size_t i = 0; i < FIELD_SPEC_COUNT; ++i) {
            if (FIELD_SPECS[i].field == field) {
                return &FIELD_SPECS[i];
            }
        }
        return nullptr;
    }

    float fieldHysteresis(DisplayField field) {
        const FieldSpec* spec = fieldSpec(field);
        return spec != nullptr ? spec->hysteresis : 0.0f;
    }

    bool fieldNeedsDegreeRing(DisplayField field) {
        const FieldSpec* spec = fieldSpec(field);
        return spec != nullptr && spec->degreeRing;
    }

    bool measurementValueAsFloat(const Sensor::Measurement& measurement, float& out) {
        if (const float* f = std::get_if<float>(&measurement.value)) {
            out = *f;
            return true;
        }
        if (const int32_t* i = std::get_if<int32_t>(&measurement.value)) {
            out = static_cast<float>(*i);
            return true;
        }
        return false;
    }

    FieldReading readDisplayField(DisplayField field, const std::vector<Sensor::Measurement>& measurements,
                                  bool snapshotValid, float elevation) {
        FieldReading reading;
        const FieldSpec* spec = fieldSpec(field);
        if (!snapshotValid || spec == nullptr) {
            return reading;
        }

        if (field == DisplayField::SeaLevelPressure && !(elevation > 0.0f)) {
            return reading;
        }
        if (field == DisplayField::VocIndex &&
            !hasFiniteMeasurement(measurements, Sensor::MeasurementType::Temperature)) {
            return reading;
        }
        if (field == DisplayField::VocIndex &&
            !hasFiniteMeasurement(measurements, Sensor::MeasurementType::RelativeHumidity)) {
            return reading;
        }

        const Sensor::Measurement* measurement = Sensor::findMeasurement(measurements, spec->measurement);
        if (measurement == nullptr) {
            return reading;
        }

        float value = 0.0f;
        if (!measurementValueAsFloat(*measurement, value) || !std::isfinite(value)) {
            return reading;
        }

        reading.value = value;
        reading.available = true;
        return reading;
    }

    size_t formatDisplayFieldLine(char* out, size_t n, DisplayField field, const FieldReading& reading) {
        if (out == nullptr || n == 0) {
            return 0;
        }
        out[0] = '\0';

        const FieldSpec* spec = fieldSpec(field);
        if (spec == nullptr) {
            return 0;
        }

        const bool available = reading.available && std::isfinite(reading.value);
        char number[24];
        if (available) {
            const int written = snprintf(number, sizeof(number), "%.*f", static_cast<int>(spec->decimals),
                                         static_cast<double>(reading.value));
            if (written < 0) {
                return 0;
            }
        } else {
            strlcpy(number, "--", sizeof(number));
        }

        int written = 0;
        if (spec->label[0] != '\0') {
            if (spec->unit[0] != '\0') {
                written = snprintf(out, n, "%s %s %s", spec->label, number, spec->unit);
            } else {
                written = snprintf(out, n, "%s %s", spec->label, number);
            }
        } else if (spec->unit[0] != '\0') {
            written = snprintf(out, n, "%s %s", number, spec->unit);
        } else {
            written = writeNumber(out, n, number);
        }
        return written < 0 ? 0 : static_cast<size_t>(written);
    }

} // namespace Display
