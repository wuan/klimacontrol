#include "actuator/ShellyChannel.h"

#include <cstdlib>
#include <cstring>

namespace Actuator {

    namespace {
        // Locate the value position for `"key"` at the top level of a flat
        // object. Shelly emits `"auto_off":false` and `"id":3, "name":"..."`,
        // i.e. optional whitespace after the colon and after commas.
        //
        // The quotes are part of the search so that `auto_off` cannot match
        // inside `auto_off_delay`, which is the one collision that actually
        // occurs in these payloads.
        const char *findValue(const char *json, const char *key) {
            if (json == nullptr || key == nullptr) {
                return nullptr;
            }
            char quoted[40];
            const size_t keyLen = std::strlen(key);
            if (keyLen + 3 > sizeof(quoted)) {
                return nullptr;
            }
            quoted[0] = '"';
            std::memcpy(quoted + 1, key, keyLen);
            quoted[keyLen + 1] = '"';
            quoted[keyLen + 2] = '\0';

            const char *at = std::strstr(json, quoted);
            if (at == nullptr) {
                return nullptr;
            }
            const char *cursor = at + keyLen + 2;
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            if (*cursor != ':') {
                return nullptr; // matched a value, not a key
            }
            ++cursor;
            while (*cursor == ' ' || *cursor == '\t') {
                ++cursor;
            }
            return cursor;
        }
    }

    bool extractBool(const char *json, const char *key, bool &out) {
        const char *v = findValue(json, key);
        if (v == nullptr) {
            return false;
        }
        if (std::strncmp(v, "true", 4) == 0) {
            out = true;
            return true;
        }
        if (std::strncmp(v, "false", 5) == 0) {
            out = false;
            return true;
        }
        return false;
    }

    bool extractNumber(const char *json, const char *key, float &out) {
        const char *v = findValue(json, key);
        if (v == nullptr) {
            return false;
        }
        char *end = nullptr;
        const float parsed = std::strtof(v, &end);
        if (end == v) {
            return false;
        }
        out = parsed;
        return true;
    }

    bool extractString(const char *json, const char *key, char *out, size_t outSize) {
        if (out == nullptr || outSize == 0) {
            return false;
        }
        out[0] = '\0';
        const char *v = findValue(json, key);
        if (v == nullptr || *v != '"') {
            return false;
        }
        ++v;
        size_t i = 0;
        while (*v != '\0' && *v != '"' && i + 1 < outSize) {
            out[i++] = *v++;
        }
        out[i] = '\0';
        // Only a closing quote means the whole value was captured; anything
        // else means it was longer than the buffer and must not be compared
        // against an expected value as if it were complete.
        return *v == '"';
    }

    bool ChannelConfig::parse(const char *json) {
        read = false;
        if (json == nullptr) {
            return false;
        }
        bool ok = extractBool(json, "auto_off", autoOff);
        ok = extractNumber(json, "auto_off_delay", autoOffDelayS) && ok;
        ok = extractString(json, "initial_state", initialState, sizeof(initialState)) && ok;
        ok = extractString(json, "in_mode", inMode, sizeof(inMode)) && ok;
        read = ok;
        return ok;
    }

    Conformance checkConformance(const ChannelConfig &config, float minAutoOffDelayS) {
        if (!config.read) {
            return Conformance::NotRead;
        }
        if (!config.autoOff) {
            return Conformance::AutoOffDisabled;
        }
        if (!(config.autoOffDelayS >= minAutoOffDelayS)) {
            return Conformance::AutoOffTooShort;
        }
        // Anything that restores or infers a previous output can re-open a
        // valve after a relay reboot with no controller alive. Only an explicit
        // "off" is safe.
        if (std::strcmp(config.initialState, "off") != 0) {
            return Conformance::InitialStateUnsafe;
        }
        if (std::strcmp(config.inMode, "detached") != 0) {
            return Conformance::InputNotDetached;
        }
        return Conformance::Ok;
    }

    const char *conformanceName(Conformance c) {
        switch (c) {
            case Conformance::Ok: return "ok";
            case Conformance::NotRead: return "not_read";
            case Conformance::AutoOffDisabled: return "auto_off_disabled";
            case Conformance::AutoOffTooShort: return "auto_off_too_short";
            case Conformance::InitialStateUnsafe: return "initial_state_unsafe";
            case Conformance::InputNotDetached: return "input_not_detached";
        }
        return "unknown";
    }

    const char *conformanceDetail(Conformance c) {
        switch (c) {
            case Conformance::Ok:
                return "Channel is safe to drive";
            case Conformance::NotRead:
                return "Could not read the channel configuration from the manifold";
            case Conformance::AutoOffDisabled:
                return "Set auto_off=true on this channel: without it nothing closes the valve if this controller stops";
            case Conformance::AutoOffTooShort:
                return "auto_off_delay is too short: a single failed request would move the valve";
            case Conformance::InitialStateUnsafe:
                return "Set initial_state=off: a relay reboot would otherwise restore the previous output with no controller running";
            case Conformance::InputNotDetached:
                return "Set in_mode=detached: a physical input could otherwise override the controller";
        }
        return "Unknown conformance state";
    }

}
