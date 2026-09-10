#ifndef KLIMACONTROL_NET_MQTT_PAYLOAD_H
#define KLIMACONTROL_NET_MQTT_PAYLOAD_H

#include <cstddef>
#include <cstdint>

#include "sensor/Sensor.h"

namespace Net {

    /**
     * Render one measurement as the MQTT JSON payload:
     * `{"time":<epoch>,"value":<v>,"unit":"..","sensor":"..","calculated":bool}`.
     * Integer values print as integers, floats with two decimals. Returns the
     * snprintf length (which exceeds `size` when the output was truncated).
     */
    int formatMeasurementPayload(char *out, size_t size, const Sensor::Measurement &m, uint32_t epoch);

} // namespace Net

#endif // KLIMACONTROL_NET_MQTT_PAYLOAD_H
