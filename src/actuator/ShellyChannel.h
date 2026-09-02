#ifndef KLIMACONTROL_ACTUATOR_SHELLYCHANNEL_H
#define KLIMACONTROL_ACTUATOR_SHELLYCHANNEL_H

#include <cstddef>
#include <cstdint>

// Pure half of the Shelly actuator: parsing a channel's reported configuration
// and deciding whether it is safe to drive.
//
// Free of Arduino and of ArduinoJson, so the parser and the conformance rule
// both run in the `native` environment against real payloads captured from the
// manifolds. That matters more here than convenience: this predicate is the
// gate that decides whether the firmware is willing to put heat into a room,
// and it is the only place the relay's own failsafe is checked.
namespace Actuator {

    // Why a hand-rolled extractor rather than a JSON library: ArduinoJson is an
    // ARDUINO-only dependency in this project, and pulling it into the native
    // build to read four flat fields would trade a small amount of parsing code
    // for the ability to test any of this off-device. The payloads are flat,
    // fixed-shape and machine-generated.
    bool extractBool(const char *json, const char *key, bool &out);
    bool extractNumber(const char *json, const char *key, float &out);
    bool extractString(const char *json, const char *key, char *out, size_t outSize);

    /**
     * The subset of `Switch.GetConfig` this firmware cares about — the fields
     * that constitute the relay's own failsafe.
     */
    struct ChannelConfig {
        bool read = false; // false when the config could not be obtained at all
        bool autoOff = false;
        float autoOffDelayS = 0.0f;
        char initialState[20] = "";
        char inMode[20] = "";

        bool parse(const char *json);
    };

    /**
     * Why a channel may not be driven. Ordered so the first failure reported is
     * the most consequential.
     */
    enum class Conformance : uint8_t {
        Ok,
        NotRead,             // no answer from the manifold
        AutoOffDisabled,     // no lease: nothing closes the valve if we die
        AutoOffTooShort,     // a single failed renewal would move the valve
        InitialStateUnsafe,  // a relay reboot would restore the previous output
        InputNotDetached     // a physical input could fight the controller
    };

    /**
     * True only when the relay will close the valve on its own if this firmware
     * stops talking to it.
     *
     * @param minAutoOffDelayS Lower bound on the lease, normally twice the
     *                         renewal interval, so one failed request cannot
     *                         move a valve that needs minutes per stroke.
     */
    Conformance checkConformance(const ChannelConfig &config, float minAutoOffDelayS);

    /** Stable machine-readable name, for the API and the logs. */
    const char *conformanceName(Conformance c);

    /** One line a human can act on. */
    const char *conformanceDetail(Conformance c);

}

#endif // KLIMACONTROL_ACTUATOR_SHELLYCHANNEL_H
