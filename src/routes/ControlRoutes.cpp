#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "SensorController.h"

#include <cmath>

#ifdef ARDUINO
#include <ArduinoJson.h>
#endif

#ifdef ARDUINO
namespace {
    // Machine-readable names, so a client can branch on the reason without
    // parsing prose or tracking enum ordinals across firmware versions.
    const char *autotuneStateName(Control::AutotuneState state) {
        switch (state) {
            case Control::AutotuneState::Idle: return "idle";
            case Control::AutotuneState::Settling: return "settling";
            case Control::AutotuneState::Oscillating: return "oscillating";
            case Control::AutotuneState::Done: return "done";
            case Control::AutotuneState::Aborted: return "aborted";
        }
        return "unknown";
    }

    const char *autotuneAbortName(Control::AutotuneAbort reason) {
        switch (reason) {
            case Control::AutotuneAbort::None: return "none";
            case Control::AutotuneAbort::UserRequested: return "user_requested";
            case Control::AutotuneAbort::CeilingBreached: return "ceiling_breached";
            case Control::AutotuneAbort::FloorBreached: return "floor_breached";
            case Control::AutotuneAbort::SensorLost: return "sensor_lost";
            case Control::AutotuneAbort::RunTimeout: return "run_timeout";
            case Control::AutotuneAbort::SettlingTimeout: return "settling_timeout";
            case Control::AutotuneAbort::AmplitudeTooSmall: return "amplitude_too_small";
            case Control::AutotuneAbort::DerivedGainsInvalid: return "derived_gains_invalid";
        }
        return "unknown";
    }
}
#endif

void WebServerManager::setupControlRoutes() {
#ifdef ARDUINO
    // GET /api/control - Live controller state and tuning parameters.
    //
    // Deliberately separate from /api/status, which every open client polls on
    // a timer and which already reopens NVS on each request. This is diagnostic
    // detail, fetched only while somebody is looking at it. No CSRF header: it
    // changes nothing.
    server.on("/api/control", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;

        doc["enabled"] = sensorController.isControlEnabled();
        doc["running"] = sensorController.isControlRunning();

        const float setpoint = sensorController.getTargetTemperature();
        doc["setpoint"] = setpoint;

        // Omitted rather than reported as NaN when there is no reading, the
        // same way /api/status already handles temperature. The error is
        // computed here so the UI cannot disagree with the controller about
        // which way round the subtraction goes.
        const float temperature = sensorController.getTemperature();
        if (!std::isnan(temperature)) {
            doc["temperature"] = temperature;
            doc["error"] = setpoint - temperature;
        }

        doc["output"] = sensorController.getControlOutput();
        doc["integral"] = sensorController.getControlIntegral();
        doc["output_min"] = SensorController::getControlOutputMin();
        doc["output_max"] = SensorController::getControlOutputMax();

        const Control::PidGains gains = sensorController.getControlGains();
        doc["kp"] = gains.kp;
        doc["ki"] = gains.ki;
        doc["kd"] = gains.kd;

        String payload;
        serializeJson(doc, payload);
        request->send(200, CONTENT_TYPE_JSON, payload);
    });

    // POST /api/temperature/target - Set target temperature
    server.on("/api/temperature/target", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {
              },
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, [[maybe_unused]] size_t total) {
                  if (index == 0) {
                      if (!verifyCsrfHeader(request)) {
                          return;
                      }

                      JsonDocument doc;
                      DeserializationError error = deserializeJson(doc, data, len);

                      if (error) {
                          request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                          return;
                      }

                      if (!doc[JSON_KEY_VALUE].is<float>()) {
                          request->send(400, CONTENT_TYPE_JSON,
                                        R"({"success":false,"error":"Value required"})");
                          return;
                      }

                      float targetTemp = doc[JSON_KEY_VALUE];

                      // Reject rather than clamp. Answering {"success":true}
                      // after quietly storing a different setpoint than the one
                      // requested tells the caller its request was honoured when
                      // it was not; a UI built on that reports a value the
                      // device is not holding until the next poll corrects it.
                      // isfinite() also screens NaN, which the clamp downstream
                      // would otherwise map to 30.0 — std::min(30.0f, NAN)
                      // returns 30.0f.
                      if (!std::isfinite(targetTemp) ||
                          targetTemp < Config::TARGET_TEMPERATURE_MIN_C ||
                          targetTemp > Config::TARGET_TEMPERATURE_MAX_C) {
                          request->send(
                              400, CONTENT_TYPE_JSON,
                              R"({"success":false,"error":"Value out of range 10.0-30.0"})");
                          return;
                      }

                      sensorController.setTargetTemperature(targetTemp);

                      request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  }
              }
    );

    // GET /api/autotune/status - Everything needed to rebuild the view.
    //
    // A run outlasts any particular browser session, so this has to be
    // self-sufficient: no client should need an earlier response to make sense
    // of this one.
    server.on("/api/autotune/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        JsonDocument doc;
        const Control::AutotuneState state = sensorController.getAutotuneState();

        doc["state"] = autotuneStateName(state);
        doc["active"] = sensorController.isAutotuneActive();
        doc["abort_reason"] = autotuneAbortName(sensorController.getAutotuneAbort());
        doc["elapsed_ms"] = sensorController.getAutotuneElapsedMs(millis());
        doc["cycles"] = sensorController.getAutotuneCycles();

        if (state == Control::AutotuneState::Done) {
            const Control::AutotuneResult &r = sensorController.getAutotuneResult();
            doc["ku"] = r.ku;
            doc["tu"] = r.tu;
            doc["derived_kp"] = r.gains.kp;
            doc["derived_ki"] = r.gains.ki;
            doc["derived_kd"] = r.gains.kd;
        }

        // The gains actually in force, so a client can show derived against
        // current without a second request.
        const Control::PidGains active = sensorController.getControlGains();
        doc["kp"] = active.kp;
        doc["ki"] = active.ki;
        doc["kd"] = active.kd;

        // No heating output exists, so a run cannot converge. Advertised here
        // rather than hard-coded in the page, so the UI stops claiming it the
        // moment the firmware can actually drive something.
        doc["can_converge"] = false;
        doc["gains_persisted"] = false;

        String payload;
        serializeJson(doc, payload);
        request->send(200, CONTENT_TYPE_JSON, payload);
    });

    // POST /api/autotune/start
    server.on("/api/autotune/start", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        // A request, not a call: the control-loop task owns the state machine.
        if (!sensorController.requestAutotuneStart()) {
            request->send(409, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"Control disabled or a run is already active"})");
            return;
        }
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/autotune/abort
    server.on("/api/autotune/abort", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        sensorController.requestAutotuneCancel();
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/autotune/accept
    server.on("/api/autotune/accept", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        if (!sensorController.acceptAutotuneResult()) {
            request->send(409, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"No converged result to accept"})");
            return;
        }
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/control/enable - Enable temperature control
    server.on("/api/control/enable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        sensorController.setControlEnabled(true);

        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // POST /api/control/disable - Disable temperature control
    server.on("/api/control/disable", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        sensorController.setControlEnabled(false);

        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });
#endif
}
