#include "network/MqttPayload.h"

#include <cstdio>
#include <variant>

namespace Net {

    int formatMeasurementPayload(char *out, const size_t size, const Sensor::Measurement &m, const uint32_t epoch) {
        const char *unit = Sensor::measurementTypeUnit(m.type);
        const char *calculated = m.calculated ? "true" : "false";
        if (const auto *i = std::get_if<int32_t>(&m.value)) {
            return snprintf(out, size,
                            R"({"time":%u,"value":%d,"unit":"%s","sensor":"%s","calculated":%s})",
                            epoch, *i, unit, m.sensor, calculated);
        }
        return snprintf(out, size,
                        R"({"time":%u,"value":%.2f,"unit":"%s","sensor":"%s","calculated":%s})",
                        epoch, static_cast<double>(std::get<float>(m.value)), unit, m.sensor, calculated);
    }

} // namespace Net
