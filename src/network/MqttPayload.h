#ifndef KLIMACONTROL_NET_MQTT_PAYLOAD_H
#define KLIMACONTROL_NET_MQTT_PAYLOAD_H

#include <cstddef>
#include <cstdint>

#include "Config.h"
#include "sensor/Sensor.h"

namespace Net {

    /**
     * Render one measurement as the MQTT JSON payload:
     * `{"time":<epoch>,"value":<v>,"unit":"..","sensor":"..","calculated":bool}`.
     * Integer values print as integers, floats with two decimals. Returns the
     * snprintf length (which exceeds `size` when the output was truncated).
     */
    int formatMeasurementPayload(char* out, size_t size, const Sensor::Measurement& m, uint32_t epoch);

    /**
     * Control-state payloads (spec mqtt-integration → "Control and actuator
     * state is published"). Each returns the snprintf length, which exceeds
     * `size` when the output was truncated — the same contract as
     * formatMeasurementPayload().
     */
    /** Setpoint as its numeric value with one decimal: `"21.5"`. */
    int formatSetpointPayload(char* out, size_t size, float setpoint);

    /** Control-enabled flag as the scalar `"ON"` or `"OFF"`. */
    int formatControlEnabledPayload(char* out, size_t size, bool enabled);

    /** PID output in [0, 1] as a whole-number percentage: `"0"`..`"100"`. */
    int formatControlOutputPayload(char* out, size_t size, float output);

    /** Confirmed heating as the scalar `"ON"` or `"OFF"`. */
    int formatHeatingPayload(char* out, size_t size, bool heating);

    /**
     * Parse a setpoint command payload. Returns true and writes `out` when the
     * whole payload is a finite number within the accepted setpoint range
     * (`Config::isValidTargetTemperature`), so the MQTT command path applies
     * exactly the same rule as `POST /api/temperature/target` — rejection, not
     * clamping. Trailing whitespace/newline is tolerated; any other trailing
     * text, an empty payload, or an out-of-range/non-finite value returns
     * false.
     */
    bool parseSetpointPayload(const char* payload, float& out);

    /**
     * Discovery topic for Home Assistant device discovery:
     * `homeassistant/device/<device_id>/config`. Keyed on the device id rather
     * than the topic prefix, so changing the prefix updates the existing device
     * in place instead of orphaning it.
     */
    int formatHaDiscoveryTopic(char* out, size_t size, const char* deviceId);

    /**
     * Render the single retained Home Assistant device-discovery payload: the
     * device block, origin, a `~` base topic, and the five components (one
     * `climate`, humidity/dew-point/PID-output sensors and a heating binary
     * sensor). Returns the snprintf length, which exceeds `size` when the
     * output was truncated — check it, the payload is the one message that can
     * outgrow the TX buffer.
     */
    int formatHaDiscoveryPayload(char* out, size_t size, const char* deviceId, const char* deviceName,
                                 const char* prefix, const char* firmwareVersion);

} // namespace Net

#endif // KLIMACONTROL_NET_MQTT_PAYLOAD_H
