#include "actuator/HeatingActuator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include "Log.h"
#else
#define TAG "actuator"
#endif

#ifdef ARDUINO
static const char *TAG = "actuator";
#endif

namespace Actuator {

    namespace {
        uint32_t since(uint32_t nowMs, uint32_t thenMs) {
            return nowMs - thenMs;
        }

#ifdef ARDUINO
        // Short enough that a dead manifold cannot occupy the Network task for
        // long, generous enough for a busy Shelly on a congested 2.4 GHz link.
        constexpr uint16_t HTTP_TIMEOUT_MS = 3000;

        bool httpGet(const char *host, const char *path, String &body) {
            if (WiFiClass::status() != WL_CONNECTED) {
                return false;
            }
            char url[192];
            snprintf(url, sizeof(url), "http://%s%s", host, path);

            HTTPClient http;
            http.setConnectTimeout(HTTP_TIMEOUT_MS);
            http.setTimeout(HTTP_TIMEOUT_MS);
            if (!http.begin(url)) {
                return false;
            }
            const int code = http.GET();
            if (code != 200) {
                http.end();
                ESP_LOGW(TAG, "%s -> HTTP %d", path, code);
                return false;
            }
            body = http.getString();
            http.end();
            return true;
        }
#endif
    }

    const char *agreementName(Agreement a) {
        switch (a) {
            case Agreement::Unknown: return "unknown";
            case Agreement::ClosedOk: return "closed";
            case Agreement::HeatingOk: return "heating";
            case Agreement::NoActuator: return "no_actuator";
            case Agreement::RelayRefused: return "relay_refused";
        }
        return "unknown";
    }

    HeatingActuator::HeatingActuator()
        : tpo(Config::DEFAULT_TPO_CYCLE_S * 1000u, Config::DEFAULT_TPO_TRAVEL_S * 1000u) {}

    bool HeatingActuator::configure(const Config::DeviceConfig &config) {
        const bool hostChanged = std::strcmp(host, config.actuator_host) != 0;
        const bool channelChanged = channel != config.actuator_channel;

        std::strncpy(host, config.actuator_host, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
        channel = config.actuator_channel;
        assigned = host[0] != '\0' && channel >= 0;

        tpo.setTiming(config.tpo_cycle_s * 1000u, config.tpo_travel_s * 1000u);

        if (hostChanged || channelChanged) {
            // A different channel is a different valve. Nothing known about the
            // old one carries over, least of all its conformance.
            lastConformance = Conformance::NotRead;
            everChecked = false;
            observed = Observation{};
            commanded = false;
        }
        return assigned;
    }

    bool HeatingActuator::sendSet([[maybe_unused]] bool on) {
#ifdef ARDUINO
        char path[64];
        snprintf(path, sizeof(path), "/rpc/Switch.Set?id=%d&on=%s", static_cast<int>(channel),
                 on ? "true" : "false");
        String body;
        if (!httpGet(host, path, body)) {
            ++failures;
            return false;
        }
        return true;
#else
        return false;
#endif
    }

    bool HeatingActuator::readStatus([[maybe_unused]] uint32_t nowMs) {
#ifdef ARDUINO
        char path[64];
        snprintf(path, sizeof(path), "/rpc/Switch.GetStatus?id=%d", static_cast<int>(channel));
        String body;
        if (!httpGet(host, path, body)) {
            ++failures;
            return false;
        }
        Observation next;
        bool output = false;
        float apower = 0.0f;
        if (!extractBool(body.c_str(), "output", output)) {
            ++failures;
            return false;
        }
        // apower may legitimately be absent on a channel with no metering; a
        // missing value must not read as "no current", which is a fault code.
        const bool havePower = extractNumber(body.c_str(), "apower", apower);
        next.valid = true;
        next.output = output;
        next.apower = havePower ? apower : -1.0f;
        next.atMs = nowMs;
        observed = next;
        return true;
#else
        return false;
#endif
    }

    bool HeatingActuator::readConfig([[maybe_unused]] uint32_t nowMs) {
#ifdef ARDUINO
        char path[64];
        snprintf(path, sizeof(path), "/rpc/Switch.GetConfig?id=%d", static_cast<int>(channel));
        String body;
        ChannelConfig cfg;
        if (httpGet(host, path, body)) {
            cfg.parse(body.c_str());
        } else {
            ++failures;
        }
        const Conformance before = lastConformance;
        lastConformance = checkConformance(cfg, MIN_LEASE_S);
        lastConformanceMs = nowMs;
        everChecked = true;
        if (lastConformance != before) {
            if (lastConformance == Conformance::Ok) {
                ESP_LOGI(TAG, "Channel %d on %s is safe to drive", static_cast<int>(channel), host);
            } else {
                ESP_LOGW(TAG, "Channel %d on %s refused: %s — %s", static_cast<int>(channel), host,
                         conformanceName(lastConformance), conformanceDetail(lastConformance));
            }
        }
        return lastConformance == Conformance::Ok;
#else
        return false;
#endif
    }

    void HeatingActuator::forceClosed(uint32_t nowMs) {
        tpo.reset();
        if (commanded) {
            sendSet(false);
        }
        commanded = false;
        lastCommandMs = nowMs;
    }

    Agreement HeatingActuator::agreement(uint32_t nowMs) const {
        if (!observed.valid || since(nowMs, observed.atMs) > OBSERVATION_STALE_MS) {
            return Agreement::Unknown;
        }
        if (!commanded) {
            return Agreement::ClosedOk;
        }
        if (!observed.output) {
            return Agreement::RelayRefused;
        }
        // A negative reading means the channel reports no power at all, so
        // absence of metering cannot be mistaken for absence of an actuator.
        if (observed.apower >= 0.0f && observed.apower < MIN_ACTUATOR_WATTS) {
            return Agreement::NoActuator;
        }
        return Agreement::HeatingOk;
    }

    void HeatingActuator::tick(float demand, bool permitted, uint32_t nowMs) {
        if (!assigned) {
            return;
        }

        // Conformance is re-read periodically because the relay can be
        // reconfigured from its own web UI at any time; a channel that stops
        // conforming must stop being driven.
        if (!everChecked || since(nowMs, lastConformanceMs) >= CONFORMANCE_RECHECK_MS) {
            readConfig(nowMs);
        }

        if (!permitted || !isConforming()) {
            if (commanded) {
                forceClosed(nowMs);
            } else {
                tpo.reset();
            }
            if (since(nowMs, lastObserveMs) >= OBSERVE_MS) {
                lastObserveMs = nowMs;
                readStatus(nowMs);
            }
            return;
        }

        const bool wantOpen = tpo.update(demand, nowMs);

        // Transitions go immediately; a steady open state is re-asserted often
        // enough that the relay's lease cannot expire underneath it. Closing is
        // explicit rather than left to the lease, because expiry would append
        // up to the whole lease duration of unrequested heat to every cycle.
        const bool transition = wantOpen != commanded;
        const bool renewDue = wantOpen && since(nowMs, lastCommandMs) >= RENEW_MS;
        if (transition || renewDue) {
            if (sendSet(wantOpen)) {
                commanded = wantOpen;
                lastCommandMs = nowMs;
            } else if (transition) {
                ESP_LOGW(TAG, "Failed to command channel %d %s", static_cast<int>(channel),
                         wantOpen ? "on" : "off");
            }
        }

        if (since(nowMs, lastObserveMs) >= OBSERVE_MS) {
            lastObserveMs = nowMs;
            readStatus(nowMs);
        }
    }

}
