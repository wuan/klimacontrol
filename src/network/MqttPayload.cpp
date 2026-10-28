#include "network/MqttPayload.h"

#include <cstdio>
#include <cstdlib>
#include <variant>

namespace Net {

    int formatMeasurementPayload(char* out, const size_t size, const Sensor::Measurement& m, const uint32_t epoch) {
        const char* unit = Sensor::measurementTypeUnit(m.type);
        const char* calculated = m.calculated ? "true" : "false";
        if (const auto* i = std::get_if<int32_t>(&m.value)) {
            return snprintf(out, size, R"({"time":%u,"value":%d,"unit":"%s","sensor":"%s","calculated":%s})", epoch, *i,
                            unit, m.sensor, calculated);
        }
        return snprintf(out, size, R"({"time":%u,"value":%.2f,"unit":"%s","sensor":"%s","calculated":%s})", epoch,
                        static_cast<double>(std::get<float>(m.value)), unit, m.sensor, calculated);
    }

    int formatSetpointPayload(char* out, const size_t size, const float setpoint) {
        return snprintf(out, size, "%.1f", static_cast<double>(setpoint));
    }

    int formatControlEnabledPayload(char* out, const size_t size, const bool enabled) {
        return snprintf(out, size, "%s", enabled ? "ON" : "OFF");
    }

    int formatControlOutputPayload(char* out, const size_t size, float output) {
        // Clamp before scaling. `!(output > 0.0f)` is true for NaN, so an
        // unavailable or nonsensical demand publishes 0 rather than "nan" —
        // Home Assistant would reject the latter as a sensor state.
        if (!(output > 0.0f)) output = 0.0f;
        if (output > 1.0f) output = 1.0f;
        const int percent = static_cast<int>(output * 100.0f + 0.5f);
        return snprintf(out, size, "%d", percent);
    }

    int formatHeatingPayload(char* out, const size_t size, const bool heating) {
        return snprintf(out, size, "%s", heating ? "ON" : "OFF");
    }

    bool parseSetpointPayload(const char* payload, float& out) {
        if (payload == nullptr || payload[0] == '\0') return false;

        char* end = nullptr;
        const float value = strtof(payload, &end);

        // strtof() consumes only a valid prefix, so "12abc" leaves `end` at 'a'
        // and is rejected rather than silently becoming 12. `end == payload`
        // catches a payload with no number at all. A trailing newline from a
        // shell publish is tolerated; anything else after the number is not.
        if (end == payload) return false;
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') {
            ++end;
        }
        if (*end != '\0') return false;

        // NaN and infinity reach here as successfully-parsed values; the range
        // predicate is what rejects them.
        if (!Config::isValidTargetTemperature(value)) return false;

        out = value;
        return true;
    }

    int formatHaDiscoveryTopic(char* out, const size_t size, const char* deviceId) {
        return snprintf(out, size, "homeassistant/device/%s/config", deviceId);
    }

    // Home Assistant device discovery: one retained message describing the
    // device and all five components, so the `device` block is sent once
    // instead of five times.
    //
    // `~` is declared inside *each component*, not once at the payload root.
    // Home Assistant only substitutes the base topic per component (its
    // SHARED_OPTIONS list, which merges device-level options into components,
    // does not include `~`; `_replace_topic_base()` runs on each component
    // config and requires the `~` key to be present there). A root-level `~`
    // is silently ignored, which would leave every topic literally "~/…" and
    // the entities permanently unavailable. Declaring it per component also
    // costs less than writing each topic out in full, which would not fit the
    // TX buffer at the longest permitted prefix (the size test pins this).
    //
    // The climate entity keeps an honest ON/OFF boolean on
    // `{prefix}/control_enabled` and does the HVAC-mode mapping in its
    // templates, so the topic stays reusable outside Home Assistant.
    // `current_temperature_topic` carries the measurement JSON, hence the
    // `value_json.value` template; the control-state topics carry plain scalars.
    int formatHaDiscoveryPayload(char* out, const size_t size, const char* deviceId, const char* deviceName,
                                 const char* prefix, const char* firmwareVersion) {
        return snprintf(
            out, size,
            "{"
            "\"dev\":{\"ids\":\"%s\",\"name\":\"%s\",\"mf\":\"Klima-Control\",\"mdl\":\"ESP32-S2 Thermostat\","
            "\"sw\":\"%s\"},"
            "\"o\":{\"name\":\"Klima-Control\",\"sw\":\"%s\"},"
            "\"avty_t\":\"~/status\","
            "\"cmps\":{"
            "\"climate\":{\"~\":\"%s\",\"p\":\"climate\",\"unique_id\":\"%s_climate\",\"name\":\"Thermostat\","
            "\"mode_stat_t\":\"~/control_enabled\",\"mode_stat_tpl\":\"{{ 'heat' if value == 'ON' else 'off' }}\","
            "\"mode_cmd_t\":\"~/control_enabled/set\",\"mode_cmd_tpl\":\"{{ 'ON' if value == 'heat' else 'OFF' }}\","
            "\"modes\":[\"off\",\"heat\"],"
            "\"curr_temp_t\":\"~/temperature\",\"curr_temp_tpl\":\"{{ value_json.value }}\","
            "\"temp_stat_t\":\"~/target_temperature\",\"temp_cmd_t\":\"~/target_temperature/set\","
            "\"min_temp\":%.0f,\"max_temp\":%.0f,\"temp_unit\":\"C\"},"
            "\"humidity\":{\"~\":\"%s\",\"p\":\"sensor\",\"unique_id\":\"%s_humidity\",\"name\":\"Humidity\","
            "\"dev_cla\":\"humidity\",\"unit_of_measurement\":\"%%\",\"stat_cla\":\"measurement\","
            "\"stat_t\":\"~/relative humidity\",\"val_tpl\":\"{{ value_json.value }}\"},"
            "\"dew_point\":{\"~\":\"%s\",\"p\":\"sensor\",\"unique_id\":\"%s_dew_point\",\"name\":\"Dew point\","
            "\"dev_cla\":\"temperature\",\"unit_of_measurement\":\"°C\",\"stat_cla\":\"measurement\","
            "\"stat_t\":\"~/dew point\",\"val_tpl\":\"{{ value_json.value }}\"},"
            "\"control_output\":{\"~\":\"%s\",\"p\":\"sensor\",\"unique_id\":\"%s_control_output\","
            "\"name\":\"PID output\",\"unit_of_measurement\":\"%%\",\"stat_cla\":\"measurement\","
            "\"stat_t\":\"~/control_output\",\"val_tpl\":\"{{ value }}\"},"
            "\"heating\":{\"~\":\"%s\",\"p\":\"binary_sensor\",\"unique_id\":\"%s_heating\",\"name\":\"Heating\","
            "\"dev_cla\":\"heat\",\"stat_t\":\"~/heating\"}"
            "}}",
            deviceId, deviceName, firmwareVersion, firmwareVersion, prefix, deviceId,
            static_cast<double>(Config::TARGET_TEMPERATURE_MIN_C),
            static_cast<double>(Config::TARGET_TEMPERATURE_MAX_C), prefix, deviceId, prefix, deviceId, prefix, deviceId,
            prefix, deviceId);
    }

} // namespace Net
