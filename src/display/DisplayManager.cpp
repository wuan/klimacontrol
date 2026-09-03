#include "display/DisplayManager.h"
#include "display/EPaperDisplay.h"

#ifdef ARDUINO

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <variant>

#include "Log.h"
#include "Network.h"
#include "SensorController.h"
#include "support/LocalTime.h"

static const char *TAG = "display";

namespace Display {

    namespace {
        // Pull a float measurement out of an already-captured snapshot. Using
        // the snapshot rather than SensorController's individual accessors
        // keeps temperature, humidity and the validity flag describing the same
        // instant — each accessor takes the lock separately.
        float floatFrom(const std::vector<Sensor::Measurement> &measurements,
                        Sensor::MeasurementType type) {
            const auto *m = Sensor::findMeasurement(measurements, type);
            if (m == nullptr) {
                return NAN;
            }
            const float *f = std::get_if<float>(&m->value);
            return f != nullptr ? *f : NAN;
        }
    } // namespace

    DisplayManager::DisplayManager(SensorController &controller)
        : controller(controller),
          policy(Config::DEFAULT_DISPLAY_INTERVAL) {
        // Created here rather than lazily: this object is a file-scope
        // singleton, and on this core FreeRTOS is already running by the time
        // static constructors execute (SensorController does the same).
        panelMutex = xSemaphoreCreateMutex();
    }

    bool DisplayManager::tryBeginForApInfo(const Config::DisplayConfig &config) {
        // Already up: the user has the normal status display enabled,
        // `setupDisplay()` brought the panel up at boot, and the same
        // panel can show the AP info on top of itself via showApInfo().
        // Short-circuit so we don't re-init a panel that's already running.
        if (enabled) {
            return true;
        }

        // Cold-boot path: `DisplayConfig.enabled == false` (the spec
        // default) and no one has called `begin()` yet.
        //
        // Step 1 — presence check. `panel.probe()` drives a manual RST
        // pulse and watches both BUSY transitions; it returns false
        // when no panel is wired up (BUSY never goes LOW), when BUSY
        // is stuck LOW (damaged panel), or when BUSY is stuck HIGH
        // (interference / damaged line). The probe touches no SPI
        // pins and no GxEPD2 state — it is purely a presence check.
        // This is what catches the no-panel case the deferred probe
        // was designed for; `panel.begin()` alone cannot, because
        // `GxEPD2::display.init()` silently succeeds with no panel.
        const uint32_t probeTimeoutMs = 250;
        if (!panel.probe(probeTimeoutMs)) {
            ESP_LOGW(TAG, "No display detected at AP-mode entry");
            return false;
        }

        // Step 2 — bring the panel up via the proven GxEPD2 init
        // path. Reached only when the probe above already verified
        // that something is responding on the connector, so the
        // panel.begin() return value reflects a genuine init fault
        // (BUSY timeout during init, etc.) rather than the silent
        // success-on-no-panel case the probe filtered out.
        if (!panel.begin(config.rotation)) {
            ESP_LOGW(TAG, "Display probe passed but panel.begin() failed at AP-mode entry");
            return false;
        }
        return true;
    }

    bool DisplayManager::begin(const Config::DisplayConfig &config, const char *deviceNameIn) {
        strlcpy(deviceName, deviceNameIn != nullptr ? deviceNameIn : "", sizeof(deviceName));

        policy = RefreshPolicy(config.interval);

        if (!panel.begin(config.rotation)) {
            ESP_LOGE(TAG, "Display initialisation faulted");
            enabled = false;
            return false;
        }

        panel.showSplash(deviceName);
        enabled = true;
        ESP_LOGI(TAG, "Display enabled (rotation=%u, min interval=%u s)",
                 config.rotation, config.interval);
        return true;
    }

    void DisplayManager::disableAndClear() {
        // Stop the Network task repainting first, so that once we hold the lock
        // there is no further work queued behind us.
        enabled = false;
        apModeActive = false;

        // Wait for any refresh already in flight. portMAX_DELAY is safe: the
        // holder is a bounded panel operation, itself capped by EPaperDisplay's
        // fault guard.
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "Could not acquire panel lock to clear the display");
            return;
        }

        // e-paper retains its image with no power, so a display that has been
        // turned off has to be actively blanked or it keeps showing a stale
        // reading forever.
        panel.clear();
        ESP_LOGI(TAG, "Display disabled and blanked");

        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

    void DisplayManager::clear() {
        // Blank the panel without changing the `enabled` flag. Used by
        // the Network task when the user submits new WiFi credentials
        // from AP mode and the device is about to restart into STA
        // mode: clearing the panel wipes the AP info (SSID + password
        // + IP) so it does not persist through the restart, while
        // leaving `enabled` alone means the user's DisplayConfig
        // preference is preserved for the next boot.
        //
        // No-op if the panel is not initialised (e.g. the normal
        // status display was disabled at boot, the panel was never
        // brought up by setupDisplay(), and the deferred probe at
        // AP-mode entry hasn't run yet).
        if (!panel.isInitialised()) {
            return;
        }
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "Could not acquire panel lock to clear");
            return;
        }
        panel.clear();
        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

    void DisplayManager::showApInfo(const char *ssid, const char *password, const char *ip) {
        if (!panel.isInitialised()) {
            return;
        }
        // Acquire the lock for the duration of the render. The normal
        // update() tick takes the same lock, so setting `apModeActive` here
        // is sufficient to suppress concurrent repaints — but we still take
        // the lock so a Network-task repaint in flight cannot race us.
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "Could not acquire panel lock for AP info");
            return;
        }
        apModeActive = true;
        panel.showApInfo(ssid, password, ip);
        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

    void DisplayManager::endApInfo() {
        apModeActive = false;
        // Hibernate only the panels we inited ourselves. A panel in
        // normal operation (`enabled == true`) is left as-is — the
        // STA-mode `update()` tick will resume painting temperature on
        // the next call.
        if (enabled) {
            return;
        }
        if (!panel.isInitialised()) {
            return;
        }
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            ESP_LOGW(TAG, "Could not acquire panel lock to hibernate");
            return;
        }
        panel.hibernate();
        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

    void DisplayManager::formatDateTime(char *out, size_t n) const {
        if (out == nullptr || n == 0) {
            return;
        }
        out[0] = '\0';
        if (network == nullptr) {
            return;
        }
        // Local time, daylight saving included. Leaves the buffer empty for
        // epoch 0 (NTP not synced), so the footer line stays blank rather than
        // claiming a date and time.
        Support::formatLocalDateHhMm(out, n, network->getCurrentEpoch());
    }

    void DisplayManager::update() {
        // Cheap pre-checks before touching the lock at all.
        //
        // `apModeActive` short-circuits the normal repaint while the AP info
        // screen is showing. The user is reading the password off the panel;
        // a temperature/humidity tick would overwrite it. The flag is cleared
        // by endApInfo() or by disableAndClear(). See change
        // 2026-09-04-ap-password-via-display.
        if (!enabled || panel.hasFaulted() || apModeActive) {
            return;
        }

        // Held for the whole body. An uncontended take is on the order of a
        // microsecond, and holding it throughout means `enabled` cannot flip
        // under us between the policy decision and the repaint — otherwise a
        // concurrent disableAndClear() could blank the panel and we would
        // immediately paint over it.
        //
        // Short timeout, not a block: if the web handler holds the lock we are
        // being disabled anyway, and any genuine change repaints on a later
        // tick because the policy only commits when a refresh actually happens.
        if (panelMutex != nullptr && xSemaphoreTake(panelMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            return;
        }

        if (enabled) {
            const SensorController::Snapshot snapshot = controller.getSnapshot();
            const float temperature = floatFrom(snapshot.measurements, Sensor::MeasurementType::Temperature);
            const float humidity = floatFrom(snapshot.measurements, Sensor::MeasurementType::RelativeHumidity);

            // Wall-clock minute drives the footer, so the panel keeps a live
            // clock. epoch/60 ticks at the same instant in every timezone (all
            // real offsets are whole minutes), so this needs no TZ awareness.
            // 0 while NTP is unsynced, matching getCurrentEpoch()'s sentinel.
            const uint32_t epoch = network != nullptr ? network->getCurrentEpoch() : 0;
            const uint32_t clockMinute = epoch / 60u;

            // Determine control state.
            //
            // Driven by the actuator's reported state rather than by demand, so
            // an unreachable manifold or a dead wax head shows as uncertain
            // instead of as a confident symbol the device cannot vouch for.
            Display::ControlState controlState;
            switch (controller.getReportedState()) {
                case Actuator::ReportedState::Disabled:
                    controlState = Display::ControlState::INACTIVE;
                    break;
                case Actuator::ReportedState::Heating:
                    controlState = Display::ControlState::ACTIVE_ON;
                    break;
                case Actuator::ReportedState::Idle:
                    controlState = Display::ControlState::ACTIVE_OFF;
                    break;
                case Actuator::ReportedState::Unknown:
                case Actuator::ReportedState::Fault:
                default:
                    controlState = Display::ControlState::UNCERTAIN;
                    break;
            }

            // Both are footer content the user can change from the web UI, so
            // they are inputs to the refresh decision, not just to the paint.
            const float target = controller.getTargetTemperature();

            // Bucket the controller demand before it reaches the refresh
            // decision. Quantising here rather than in RefreshPolicy keeps the
            // hysteresis with the value it smooths, and leaves the policy a
            // plain equality test. Without it a live output would change on
            // every tick and hold the panel at its minimum-interval floor
            // permanently.
            const float outLo = SensorController::getControlOutputMin();
            const float outHi = SensorController::getControlOutputMax();
            const float span = (outHi - outLo) != 0.0f ? (outHi - outLo) : 1.0f;
            const float demandFraction = (controller.getControlOutput() - outLo) / span;
            demandBucket = Display::nextDemandBucket(demandFraction, demandBucket);

            const RefreshKind kind = policy.evaluate(temperature, humidity, snapshot.valid,
                                                     millis(), clockMinute, target, controlState,
                                                     demandBucket);
            if (kind != RefreshKind::None) {
                const bool available =
                    snapshot.valid && !std::isnan(temperature) && !std::isnan(humidity);

                char tempStr[16];
                char humStr[16];
                formatTemperature(tempStr, sizeof(tempStr), temperature, available);
                formatHumidity(humStr, sizeof(humStr), humidity, available);

                char dateTime[16];
                formatDateTime(dateTime, sizeof(dateTime));

                // Format setpoint
                char setpointStr[8];
                if (std::isnan(target)) {
                    snprintf(setpointStr, sizeof(setpointStr), "--");
                } else {
                    snprintf(setpointStr, sizeof(setpointStr), "%.1f", static_cast<double>(target));
                }

                // Bare numbers: EPaperDisplay owns the unit decoration, because
                // the degree mark has to be drawn geometrically (the GFX fonts
                // only carry glyphs 0x20-0x7E) rather than printed.
                panel.render(tempStr, humStr, deviceName, dateTime, controlState, setpointStr,
                             demandBucket, kind);
            }
        }

        if (panelMutex != nullptr) {
            xSemaphoreGive(panelMutex);
        }
    }

} // namespace Display

#endif // ARDUINO
