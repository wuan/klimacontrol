#include "WebServerManager.h"
#include "routes/RouteHelpers.h"

#include "Config.h"
#include "Network.h"
#include "SensorController.h"
#include "support/RequestDiag.h"

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
    //
    // Exact matching for the same reason /api/actuator needs it: the default
    // BackwardCompatible matcher also matches any deeper path, and handlers are
    // tried in registration order, so a plain "/api/control" registered first
    // would swallow "/api/control/tuning". Only the differing HTTP method keeps
    // that from biting today, which is not a property anybody can see at the
    // call site.
    server.on(AsyncURIMatcher::exact("/api/control"), HTTP_GET, [this](AsyncWebServerRequest *request) {
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

        doc["state"] = Actuator::reportedStateName(sensorController.getReportedState());
        doc["output"] = sensorController.getControlOutput();
        doc["integral"] = sensorController.getControlIntegral();
        doc["output_min"] = SensorController::getControlOutputMin();
        doc["output_max"] = SensorController::getControlOutputMax();

        // From the running controller, not from DeviceConfig. A gain change is
        // applied by the control task on a later tick, so reporting the stored
        // values would show a pending change as already in force — and reading
        // this back is exactly how a caller confirms that it landed.
        const Control::PidGains gains = sensorController.getControlGains();
        doc["kp"] = gains.kp;
        doc["ki"] = gains.ki;
        doc["kd"] = gains.kd;
        doc["control_interval_s"] = config.getDeviceConfig().control_interval_s;

        String payload;
        serializeJson(doc, payload);
        request->send(200, CONTENT_TYPE_JSON, payload);
    });

    // POST /api/control/tuning - the PID gains and the control interval.
    //
    // Exact matcher, as above. All four fields are required and validated
    // together: gains are only meaningful as a set, so accepting three and
    // reverting one would leave a controller nobody configured.
    server.on(AsyncURIMatcher::exact("/api/control/tuning"), HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {},
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index != 0) {
                      return;
                  }
                  if (!verifyCsrfHeader(request)) {
                      return;
                  }
                  JsonDocument doc;
                  if (deserializeJson(doc, data, len)) {
                      request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                      return;
                  }

                  // Absent fields are rejected rather than defaulted from the
                  // current tuning. A form that failed to send `ki` would
                  // otherwise silently keep the old one and report success,
                  // which is indistinguishable from having stored what the user
                  // typed.
                  auto missing = [&](const char *field) {
                      String body = R"({"success":false,"error":")";
                      body += field;
                      body += R"( required"})";
                      request->send(400, CONTENT_TYPE_JSON, body);
                  };
                  if (!doc["kp"].is<float>()) { missing("kp"); return; }
                  if (!doc["ki"].is<float>()) { missing("ki"); return; }
                  if (!doc["kd"].is<float>()) { missing("kd"); return; }
                  if (!doc["control_interval_s"].is<int>()) {
                      missing("control_interval_s");
                      return;
                  }

                  const float kp = doc["kp"];
                  const float ki = doc["ki"];
                  const float kd = doc["kd"];
                  const int intervalS = doc["control_interval_s"];

                  // Rejected rather than clamped, and the offending field is
                  // named: the result drives a physical valve, so a silently
                  // altered gain is indistinguishable from an accepted one, and
                  // an all-or-nothing validation that does not say which field
                  // failed leaves the user guessing. isfinite() screens NaN,
                  // which a bare range comparison would pass on both sides.
                  auto reject = [&](const char *field, const char *range) {
                      String body = R"({"success":false,"error":")";
                      body += field;
                      body += " out of range ";
                      body += range;
                      body += R"(","field":")";
                      body += field;
                      body += R"("})";
                      request->send(400, CONTENT_TYPE_JSON, body);
                  };
                  if (!std::isfinite(kp) || kp < Config::MIN_PID_KP || kp > Config::MAX_PID_KP) {
                      // Zero lands here by construction: a zero proportional
                      // gain disables control while control still reports
                      // itself as enabled.
                      reject("kp", "0.01-100");
                      return;
                  }
                  if (!std::isfinite(ki) || ki < Config::MIN_PID_KI || ki > Config::MAX_PID_KI) {
                      reject("ki", "0-0.05");
                      return;
                  }
                  if (!std::isfinite(kd) || kd < Config::MIN_PID_KD || kd > Config::MAX_PID_KD) {
                      reject("kd", "0-600");
                      return;
                  }
                  if (intervalS < static_cast<int>(Config::MIN_CONTROL_INTERVAL_S) ||
                      intervalS > static_cast<int>(Config::MAX_CONTROL_INTERVAL_S)) {
                      reject("control_interval_s", "1-600");
                      return;
                  }

                  // Persists, then asks the control task to adopt it. Success
                  // therefore means accepted, not yet in force; GET
                  // /api/control reports the gains actually running.
                  sensorController.requestGains(Control::PidGains{kp, ki, kd},
                                                static_cast<uint16_t>(intervalS));
                  request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
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

    // Exact matching, not the default.
    //
    // server.on(const char*) builds a BackwardCompatible matcher, which matches
    // `_value == path || path.startsWith(_value + "/")` (WebServer.cpp:335). So
    // a plain "/api/actuator" also swallows "/api/actuator/timing" and
    // "/api/actuator/recheck", and because handlers are tried in registration
    // order it wins. That produced two failures at once: recheck (no body) hit
    // the assignment handler's empty onRequest and returned 501, and timing
    // (with a body) was parsed as an assignment with no host — silently
    // clearing the channel and answering 200.
    //
    // Registering the specific paths first would also work, as SensorRoutes
    // happens to do for /api/sensors, but that is a property nobody can see at
    // the call site and a later reordering would quietly reintroduce this.
    // GET /api/actuator - assignment, conformance and what the relay is doing.
    server.on(AsyncURIMatcher::exact("/api/actuator"), HTTP_GET, [this](AsyncWebServerRequest *request) {
        const Actuator::HeatingActuator &act = network.getHeatingActuator();
        const Config::DeviceConfig &cfg = config.getDeviceConfig();
        const uint32_t now = millis();

        JsonDocument doc;
        doc["host"] = cfg.actuator_host;
        doc["channel"] = cfg.actuator_channel;
        doc["assigned"] = act.isAssigned();
        doc["conformance"] = Actuator::conformanceName(act.conformance());
        doc["conformance_detail"] = Actuator::conformanceDetail(act.conformance());
        doc["conforming"] = act.isConforming();
        doc["conformance_checks"] = act.conformanceChecks();
        doc["conformance_age_ms"] = act.conformanceAgeMs(now);
        doc["ever_checked"] = act.everConformanceChecked();
        doc["permitted"] = sensorController.isHeatingPermitted();
        doc["safety_shutoff"] = sensorController.isSafetyShutoffEngaged();
        doc["commanded_open"] = act.commandedOpen();
        doc["duty"] = act.latchedDuty();
        doc["failed_requests"] = act.failedRequests();
        doc["cycle_s"] = cfg.tpo_cycle_s;
        doc["travel_s"] = cfg.tpo_travel_s;
        doc["safety_max_c"] = cfg.safety_max_c;

        const Actuator::Observation &obs = act.observation();
        doc["observed_valid"] = obs.valid;
        if (obs.valid) {
            doc["observed_output"] = obs.output;
            doc["observed_power_w"] = obs.apower;
            doc["observed_age_ms"] = now - obs.atMs;
        }
        doc["agreement"] = Actuator::agreementName(act.agreement(now));

        String payload;
        serializeJson(doc, payload);
        request->send(200, CONTENT_TYPE_JSON, payload);
    });

    // POST /api/actuator - assign this device to a manifold channel.
    server.on(AsyncURIMatcher::exact("/api/actuator"), HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {},
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  if (index != 0) {
                      return;
                  }
                  if (!verifyCsrfHeader(request)) {
                      return;
                  }
                  JsonDocument doc;
                  if (deserializeJson(doc, data, len)) {
                      request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                      return;
                  }
                  const char *host = doc["host"] | "";
                  const int ch = doc["channel"] | -1;

                  // Host and channel are validated together: a host with no
                  // channel, or a channel with no host, is not something that
                  // can be acted on, so it clears the assignment rather than
                  // being stored half-complete.
                  if (host[0] != '\0' &&
                      (ch < 0 || ch > static_cast<int>(Config::MAX_ACTUATOR_CHANNEL))) {
                      request->send(400, CONTENT_TYPE_JSON,
                                    R"({"success":false,"error":"channel must be 0-3"})");
                      return;
                  }
                  config.updateActuatorAssignment(host, static_cast<int8_t>(ch));
                  request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
              });

    // POST /api/actuator/timing - cycle, travel and the safety limit.
    server.on("/api/actuator/timing", HTTP_POST,
              []([[maybe_unused]] AsyncWebServerRequest *request) {},
              nullptr,
              [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index,
                     [[maybe_unused]] size_t total) {
                  Support::markStage(Support::StageBodyEntered);
                  if (index != 0) {
                      return;
                  }
                  if (!verifyCsrfHeader(request)) {
                      return;
                  }
                  Support::markStage(Support::StageCsrfPassed);
                  JsonDocument doc;
                  if (deserializeJson(doc, data, len)) {
                      request->send(400, CONTENT_TYPE_JSON, JSON_RESPONSE_ERROR_INVALID_JSON);
                      return;
                  }
                  Support::markStage(Support::StageJsonParsed);
                  const Config::DeviceConfig &cur = config.getDeviceConfig();
                  const int cycleS = doc["cycle_s"] | static_cast<int>(cur.tpo_cycle_s);
                  const int travelS = doc["travel_s"] | static_cast<int>(cur.tpo_travel_s);
                  const float safeMax = doc["safety_max_c"] | cur.safety_max_c;
                  const float safeHyst = doc["safety_hyst_c"] | cur.safety_hyst_c;

                  // Rejected rather than clamped, and the pair is checked
                  // together: a cycle that cannot fit several full strokes
                  // silently reduces the controller to bang-bang, so it must
                  // not be accepted and quietly corrected.
                  if (cycleS < Config::MIN_TPO_CYCLE_S || cycleS > Config::MAX_TPO_CYCLE_S ||
                      travelS < Config::MIN_TPO_TRAVEL_S || travelS > Config::MAX_TPO_TRAVEL_S) {
                      request->send(400, CONTENT_TYPE_JSON,
                                    R"({"success":false,"error":"cycle or travel time out of range"})");
                      return;
                  }
                  if (cycleS < travelS * static_cast<int>(Config::TPO_MIN_STROKES_PER_CYCLE)) {
                      request->send(
                          400, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"cycle must be at least 4x the actuator travel time"})");
                      return;
                  }
                  if (!std::isfinite(safeMax) || safeMax < Config::MIN_SAFETY_MAX_C ||
                      safeMax > Config::MAX_SAFETY_MAX_C || !std::isfinite(safeHyst) ||
                      safeHyst <= 0.0f || safeHyst > 10.0f) {
                      request->send(400, CONTENT_TYPE_JSON,
                                    R"({"success":false,"error":"safety limit or hysteresis out of range"})");
                      return;
                  }
                  Support::markStage(Support::StageValidated);
                  config.updateActuatorTiming(static_cast<uint16_t>(cycleS),
                                              static_cast<uint16_t>(travelS), safeMax, safeHyst);
                  request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
                  Support::markStage(Support::StageResponded);
              });

    // POST /api/actuator/recheck - re-read the channel config now.
    server.on("/api/actuator/recheck", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (!verifyCsrfHeader(request)) {
            return;
        }
        network.requestActuatorRecheck();
        request->send(200, CONTENT_TYPE_JSON, JSON_RESPONSE_SUCCESS);
    });

    // GET /api/diag/requests - the RAM ring. A GET, deliberately: the fault it
    // exists to observe affects body-carrying POSTs, so the readout must not
    // travel by the same route as the thing it is measuring.
    server.on("/api/diag/requests", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["total"] = Support::totalRequests();
        doc["held"] = Support::requestCount();
        JsonArray arr = doc["requests"].to<JsonArray>();
        char stages[48];
        for (size_t i = 0; i < Support::requestCount(); ++i) {
            const Support::RequestRecord &r = Support::requestAt(i);
            Support::describeStages(r.stages, stages, sizeof(stages));
            JsonObject o = arr.add<JsonObject>();
            o["at_ms"] = r.atMs;
            o["method"] = r.method;
            o["url"] = r.url;
            o["code"] = r.code; // -1 = handler produced no response
            o["stages"] = stages;
            o["len"] = r.contentLength;
            o["ms"] = r.elapsedMs;
            o["heap"] = r.freeHeap;
            o["blk"] = r.largestBlock;
            o["ctype"] = r.contentType;
            // Non-zero means the framework parsed the body as form parameters
            // instead of handing it to the body callback.
            o["params"] = r.params;
        }
        String payload;
        serializeJson(doc, payload);
        request->send(200, CONTENT_TYPE_JSON, payload);
    });

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

        // A run can only converge if the output can actually deliver heat, so
        // the plant responds to the relay. Derived rather than hard-coded: an
        // earlier version pinned this to false and the warning could therefore
        // never clear, which is precisely the bug that wording was meant to
        // avoid.
        const Actuator::HeatingActuator &act = network.getHeatingActuator();
        const bool canConverge = act.isAssigned() && act.isConforming();
        doc["can_converge"] = canConverge;
        if (!canConverge) {
            doc["converge_note"] =
                act.isAssigned()
                    ? "The assigned actuator channel is refused, so nothing can drive the room and a run will end in a timeout."
                    : "No actuator channel is assigned, so nothing can drive the room and a run will end in a timeout.";
        }

        // Accepted gains are stored in NVS and survive a restart. Kept as a
        // field rather than dropped so a UI built against the old firmware
        // stops warning about durability instead of silently reading `false`
        // from a missing key.
        doc["gains_persisted"] = true;

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
        // Persists synchronously, then hands the change to the control task.
        // So success means the result was validated, stored and accepted — not
        // that the running controller has already adopted it. A caller confirms
        // that by reading the gains in force from GET /api/control, which is
        // also what makes the change observable in the UI.
        if (!sensorController.acceptAutotuneResult()) {
            request->send(409, CONTENT_TYPE_JSON,
                          R"({"success":false,"error":"No converged result to accept"})");
            return;
        }
        request->send(200, CONTENT_TYPE_JSON,
                      R"({"success":true,"accepted":true,"persisted":true})");
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
