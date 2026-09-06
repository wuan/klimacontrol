#include "control/TemperatureController.h"
#include "Log.h"

#include <algorithm>
#include <cmath>

#ifdef ARDUINO
static const char* TAG = "control";
#else
// On native, the ESP_LOG* macros are no-ops; their tag argument is
// discarded. Define TAG as a macro so there is no unused-variable to
// warn about.
#define TAG "control"
#endif

namespace {
    // The gains are no longer compiled in — they come from DeviceConfig — so
    // only the output clamps are constants here. Defined in
    // control/PidController.h so the API can report them.
    constexpr float MaxOutput = Control::DEFAULT_MAX_OUTPUT;
    constexpr float MinOutput = Control::DEFAULT_MIN_OUTPUT;
}

namespace Control {

    TemperatureController::TemperatureController(Config::ConfigManager &config)
        : config(config),
          // The config cache holds the compiled-in defaults at this point: this
          // object is a global constructed long before config.begin() has read
          // NVS. begin() applies the stored tuning once it is available.
          pid(Control::PidGains{config.getDeviceConfig().kp, config.getDeviceConfig().ki,
                                config.getDeviceConfig().kd},
              MinOutput, MaxOutput),
          autotuner(Control::AutotuneLimits{}) {
    }

    void TemperatureController::begin() {
        // Adopt the stored tuning. This has to happen here rather than in the
        // constructor: this object is a global, constructed before config.begin()
        // has read NVS, so the constructor could only ever see the compiled-in
        // defaults. Called from setup() before the Sensor Monitor task exists, so
        // writing PID state directly is safe — there is no other task to race.
        const Config::DeviceConfig &cfg = config.getDeviceConfig();
        pid.setGains(Control::PidGains{cfg.kp, cfg.ki, cfg.kd});
        ESP_LOGI(TAG, "PID tuning from config: Kp=%.4f Ki=%.5f Kd=%.1f interval=%us",
                 static_cast<double>(cfg.kp), static_cast<double>(cfg.ki),
                 static_cast<double>(cfg.kd), static_cast<unsigned>(cfg.control_interval_s));
    }

    void TemperatureController::setTargetTemperature(float temperature) {
        // Last line of defence for callers that do not come through HTTP — chiefly
        // main.cpp restoring the persisted value at boot. Requests arriving via
        // POST /api/temperature/target are range-checked and *rejected* by the
        // route handler before they get here, because silently substituting a
        // different setpoint than the one asked for is not an acceptable answer to
        // a user. See spec `temperature-control` → "Setpoint range" for the three
        // validation layers and what each is for.
        float clamped = std::max(Config::TARGET_TEMPERATURE_MIN_C,
                                 std::min(Config::TARGET_TEMPERATURE_MAX_C, temperature));
        ESP_LOGI(TAG, "Target temperature set to %.1f C", clamped);

        // Persist to NVS using partial update (also updates in-memory cache)
        config.updateTargetTemperature(clamped);
    }

    void TemperatureController::setControlEnabled(bool enabled) {
        // Configuration write only. This runs on the web-server task, while the PID
        // accumulators are owned by the Sensor Monitor task; resetting them from
        // here would race a control tick that is mid read-modify-write and could
        // silently lose the reset. update() detects the resumption itself.
        if (config.getDeviceConfig().temperature_control_enabled != enabled) {
            ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");

            // Persist to NVS using partial update (also updates in-memory cache)
            config.updateTemperatureControlEnabled(enabled);
        }
    }

    bool TemperatureController::requestAutotuneStart() {
        // Early refusal on the web task so the API can answer immediately. The
        // control loop re-checks both conditions, because it is authoritative and
        // the state can move between here and the next tick.
        if (!config.getDeviceConfig().temperature_control_enabled) {
            return false;
        }
        if (isAutotuneActive()) {
            return false;
        }
        autotuneStartRequested.store(true);
        return true;
    }

    void TemperatureController::requestAutotuneCancel() {
        autotuneCancelRequested.store(true);
    }

    bool TemperatureController::acceptAutotuneResult() {
        if (autotuner.state() != Control::AutotuneState::Done) {
            return false;
        }
        // Persist first, then request. A run costs hours of the plant's time, so
        // the gains the user is told were accepted have to be the ones that will
        // survive a restart: if the write is the step that fails, it fails before
        // the running controller has adopted anything.
        const Control::PidGains derived = autotuner.getResultGains();
        requestGains(derived, config.getDeviceConfig().control_interval_s);
        ESP_LOGI(TAG, "Autotune gains stored, awaiting control tick: Kp=%.4f Ki=%.5f Kd=%.1f",
                 static_cast<double>(derived.kp), static_cast<double>(derived.ki),
                 static_cast<double>(derived.kd));
        return true;
    }

    void TemperatureController::requestGains(Control::PidGains gains, uint16_t controlIntervalS) {
        // Runs on the web task. Persisting here is safe — it touches only NVS and
        // the config cache — but applying the gains is not, so that is handed to
        // the control task rather than done here. See the gainsChangeRequested
        // comment for the race this avoids.
        config.updateTuning(gains.kp, gains.ki, gains.kd, controlIntervalS);

        // Publish the validated values as stored, not as supplied: updateTuning()
        // may have fallen back on a field, and the running controller must agree
        // with what was persisted rather than with what was asked for.
        const Config::DeviceConfig &cfg = config.getDeviceConfig();
        pendingGains = Control::PidGains{cfg.kp, cfg.ki, cfg.kd};
        gainsChangeRequested.store(true, std::memory_order_release);
    }

    bool TemperatureController::isHeatingPermitted() const {
        // Snapshot the cross-task config read so the enabled flag is observed
        // consistently with anything else the same caller might want from the
        // struct. isHeatingPermitted() only reads one field today, but it runs
        // from both the Network task and the AsyncTCP web task and the writer
        // (a web-task updateTemperatureControlEnabled) is on the AsyncTCP task.
        //
        // lastInputValid stands in for the live sensor read this used to do:
        // it is what the most recent update() was handed, so at most one
        // Sensor Monitor tick stale, and false until that first tick.
        return config.getDeviceConfigSnapshot().temperature_control_enabled &&
               !safetyShutoff && lastInputValid;
    }

    void TemperatureController::suspendPid(uint32_t nowMs) {
        pid.suspend();
        lastPidComputeMs = nowMs;
    }

    float TemperatureController::update(float temperature, bool valid, uint32_t nowMs) {
        const uint32_t now = nowMs;

        // Record what this tick was handed before anything can return early, so
        // isHeatingPermitted() always describes the most recent input.
        lastInputValid = valid && !std::isnan(temperature);

        // Take an indivisible snapshot of every DeviceConfig field this tick
        // reads, before any of them. The web task can be in the middle of an
        // updateActuatorTiming() (or any other updateXxx()) on a different core,
        // and reading through the const reference would let this tick see the
        // new safety limit paired with the old hysteresis, the new target
        // paired with the old control interval, or any other half-updated
        // combination. The snapshot collapses the read set to one indivisible
        // copy, mirroring the gains pattern in the header's pendingGains comment.
        const Config::DeviceConfig cfg = config.getDeviceConfigSnapshot();

        // Adopt a pending gain change first, on the only task allowed to write PID
        // state. setGains() suspends, so whichever path below runs on this tick
        // treats the change as the discontinuity it is: the next computing tick
        // restarts bumplessly rather than carrying an integral accumulated under
        // the old gains into the new ones.
        if (gainsChangeRequested.exchange(false, std::memory_order_acquire)) {
            const Control::PidGains gains = pendingGains;
            pid.setGains(gains);
            lastPidComputeMs = now;
            ESP_LOGI(TAG, "PID gains applied: Kp=%.4f Ki=%.5f Kd=%.1f",
                     static_cast<double>(gains.kp), static_cast<double>(gains.ki),
                     static_cast<double>(gains.kd));
        }

        // Over-temperature shutoff, evaluated before anything else so a saturated
        // integral cannot override it, and latched so releasing needs the
        // temperature to fall a hysteresis band below the limit rather than merely
        // touch it. An unavailable reading engages it too: an unknown temperature
        // is not a safe basis for delivering heat. Both the limit and the
        // hysteresis are taken from the snapshot taken above so a writer that
        // changes one without the other cannot produce a torn observation.
        {
            const float t = temperature;
            if (std::isnan(t) || !valid) {
                if (!safetyShutoff) {
                    ESP_LOGW(TAG, "control: invalid data shutoff");
                }
                safetyShutoff = true;
            } else if (t > cfg.safety_max_c) {
                if (!safetyShutoff) {
                    ESP_LOGW(TAG, "control: over-temperature shutoff: %.1f C above limit %.1f C",
                             static_cast<double>(t), static_cast<double>(cfg.safety_max_c));
                }
                safetyShutoff = true;
            } else if (safetyShutoff && t < cfg.safety_max_c - cfg.safety_hyst_c) {
                ESP_LOGI(TAG, "control: over-temperature shutoff released at %.1f C", static_cast<double>(t));
                safetyShutoff = false;
            }
        }

        if (safetyShutoff) {
            suspendPid(now);
            autotuner.cancel();
            lastControlOutput = 0.0f;
            return 0.0f;
        }

        // Cross-task requests are consumed here, on the only task allowed to mutate
        // autotuner state. Cancel first, so a cancel and a start arriving in the
        // same tick mean "stop the old run, begin a new one" rather than resolving
        // by arrival order.
        if (autotuneCancelRequested.exchange(false)) {
            autotuner.cancel();
        }
        if (autotuneStartRequested.exchange(false)) {
            if (cfg.temperature_control_enabled && !isAutotuneActive()) {
                ESP_LOGI(TAG, "Autotune starting around %.1f C",
                         static_cast<double>(cfg.target_temperature));
                autotuner.start(cfg.target_temperature, now);
            }
        }

        if (isAutotuneActive()) {
            // A run switched off underneath itself would be driving an output
            // nobody enabled.
            if (!cfg.temperature_control_enabled) {
                autotuner.cancel();
                suspendPid(now);
                lastControlOutput = 0.0f;
                return 0.0f;
            }

            // The autotuner owns the output for the duration. Suspending the PID is
            // what makes the handover back correct: the run is exactly the kind of
            // gap the bumpless-restart path was built for.
            suspendPid(now);
            const float currentTemp = temperature;
            const bool inputValid = valid && !std::isnan(currentTemp);
            lastControlOutput = autotuner.update(currentTemp, inputValid, now);

            if (!isAutotuneActive()) {
                ESP_LOGI(TAG, "Autotune finished: state=%d reason=%d cycles=%u",
                         static_cast<int>(autotuner.state()),
                         static_cast<int>(autotuner.abortReason()),
                         static_cast<unsigned>(autotuner.completedCycles()));
            }
            return lastControlOutput;
        }

        float currentTemp = temperature;
        if (!cfg.temperature_control_enabled || !valid ||
            std::isnan(currentTemp)) {
            // Tell the PID it skipped a tick, so the next one that does run
            // restarts bumplessly instead of charging its integral with the whole
            // elapsed gap. All three of the disabled, no-valid-data and NaN cases
            // land here, and all three leave the same stale-timestamp trap.
            suspendPid(now);

            // The stored output must reflect reality on every call, otherwise
            // isControlActive() keeps reporting the last positive output after the
            // sensor drops out or control is switched off.
            lastControlOutput = 0.0f;

            if (cfg.temperature_control_enabled) {
                if (!valid) {
                    ESP_LOGI(TAG, "control: no valid data");
                }
                if (std::isnan(currentTemp)) {
                    ESP_LOGI(TAG, "control: currentTemp is NaN");
                }
            }
            return 0.0f;
        }

        // Everything above this point runs on every sensor tick: the
        // over-temperature shutoff, because a safety limit noticed up to a full
        // control interval late is a weaker guarantee than the one the spec
        // describes; and the autotuner, because it measures the amplitude of an
        // induced oscillation and coarser sampling misses peaks, which
        // under-estimates `a`, over-estimates `Ku` and yields gains more aggressive
        // than the plant can take — from a run that reports convergence. Only the
        // PID computation below is decimated.
        const uint32_t intervalMs =
            static_cast<uint32_t>(cfg.control_interval_s) * 1000u;
        if (now - lastPidComputeMs < intervalMs) {
            // A tick between computations, not a skipped one — so pointedly no
            // suspendPid() here. Suspending would make every computation a bumpless
            // restart and the integral would never accumulate past a single
            // interval, leaving `ki` with no effect whatever it was set to.
            //
            // lastControlOutput is left at the last computed value rather than
            // zeroed, so demand is held across the interval. Zeroing it would make
            // the actuator see one pulse per interval, and would make
            // isControlActive() flicker off between computations.
            return lastControlOutput;
        }
        lastPidComputeMs = now;

        const bool restarting = !pid.isRunning();

        float targetTemperature = cfg.target_temperature;
        float error = targetTemperature - currentTemp;
        float output = pid.update(error, now);
        ESP_LOGI(TAG, "control update (p: %.2f, i: %.4f, d: %.2f): %.1f K -> %0.2f",
                 static_cast<double>(cfg.kp),
                 static_cast<double>(cfg.ki),
                 static_cast<double>(cfg.kd),
                 static_cast<double>(error),
                 static_cast<double>(output)
                 );

        lastControlOutput = output;

        if (restarting) {
            ESP_LOGD(TAG, "PID restart: T=%.1f C (target=%.1f C), output=%.2f (proportional only)",
                     currentTemp, targetTemperature, output);
        } else {
            ESP_LOGD(TAG, "PID: T=%.1f C (target=%.1f C), output=%.2f, I=%.2f", currentTemp,
                     targetTemperature, output, pid.getIntegral());
        }

        return output;
    }

}
