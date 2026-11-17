# KlimaControl Power Optimization Guide

---

## 🎯 TL;DR - Current Status (2026-11-17)

There are now two builds, and they do not share the same power-management
ceiling:

- **Arduino environment (`adafruit_qtpy_esp32s2`)**: still uses the precompiled
  Arduino 2.0.17 SDK with `CONFIG_PM_ENABLE` off. `esp_pm_configure()` cannot
  enable DFS or light sleep here. This is the reason the project adopted the
  ESP-IDF environment in the first place.
- **ESP-IDF environment (`adafruit_qtpy_esp32s2_idf`)**: owns `sdkconfig.defaults`
  and has `CONFIG_PM_ENABLE=y`. DFS at 80/40 MHz is enabled in `setup()` and
  **kept**. Automatic light sleep was tried and **reverted**: the USB CDC console
  stopped delivering output and a Network-task panic followed
  (`heap_caps_malloc_prefer`). WiFi/HTTP/MQTT survived, but the USB-only test
  unit needs its console for recovery.

Measured evidence lives in `docs/BUILD_PARITY_BASELINE.md` §"Phase 4
power-management experiments". The PM mode counters show 40 MHz (`APB_MIN`) for
more than half of a 213 s window, with WiFi holding the APB lock for the rest.
Idle and transmit current are **still unmeasured** — no meter is connected to
the capture host — so any mA figure below remains a planning estimate, not a
measurement of this build.

### Recommended next steps

```
✅ DONE / KEPT:
   - IDF build owns sdkconfig; DFS 80/40 MHz enabled (Phase 4.1)
   - Configurable WiFi TX power and sleep mode; per-sensor read intervals

⚠️ STILL OPEN:
   - WiFi power-save observation window (MIN_MODEM vs MAX_MODEM/NONE)
   - Adaptive TX power based on RSSI
   - Brownout-threshold decision, coordinated with the display observation

❌ REVERTED:
   - Automatic light sleep with USB CDC attached: console lost, Network panic
   - esp_pm_configure() in the Arduino environment: CONFIG_PM_ENABLE is off
```

**Realistic savings are still unquantified on this hardware** until a meter is
attached; do not quote the old 20–40 mA estimate as a result.

---

## Executive Summary

The Arduino environment cannot use `esp_pm_configure()` because its precompiled
SDK has `CONFIG_PM_ENABLE` off. The ESP-IDF environment removes that blocker:
DFS is enabled and measured, while automatic light sleep was tried and reverted
because it broke the USB CDC console and was followed by a Network-task panic.
The remaining savings must come from WiFi power save, radio power and read
intervals, not from Arduino-side PM tricks.

### Planning Estimates (not measurements of this build)
- **Active state (transmitting MQTT):** ~200–250 mA
- **Idle state:** ~80–120 mA (WiFi sleep disabled, ~100 mA baseline)
- **With WIFI_PS_MIN_MODEM:** ~30–60 mA (IF WiFi stays stable)

These are the March 2026 planning figures. No meter was connected during the
November 2026 Phase 4 experiments, so they are **not** a measurement of the IDF
build.

### Remaining Optimizations
- **WiFi power-save observation:** −30–50 mA if stable — still needs the declared
  observation window; the test unit already runs `MAX_MODEM`
- **Per-sensor read intervals:** already centralised at 15 s
  (`SensorController::MEASUREMENT_INTERVAL_MS`), with SGP40 at 1 Hz
- **Adaptive TX power:** −5–20 mA (open; fixed TX power is configurable today)

---

## ⚠️ IMPORTANT: Arduino Framework Limitations (still true)

The precompiled Arduino 2.0.17 SDK does not set `CONFIG_PM_ENABLE`. In that
environment:
- ❌ `esp_pm_configure()` returns `ESP_ERR_NOT_SUPPORTED`
- ❌ Light sleep and DFS are unavailable
- ❌ The `POWER_OPTIMIZATION.md` limitations from Feb–Mar 2026 still apply

The IDF environment (`adafruit_qtpy_esp32s2_idf`) is different: it owns
`sdkconfig.defaults` and enables PM. Do not apply the Arduino limitation
statements to the IDF build; use the measured Phase 4 results instead.

---

## Critical Issues to Fix (Priority: 🔴 HIGH)

### 1. WiFi Sleep Mode (configurable, observation still pending)
**Location:** `src/network/WifiStation.cpp` (`applyEnergyConfig()`),
`Config::EnergyConfig::wifi_sleep_mode`

**Current code:**
```cpp
WiFi.setSleep(sleepMode); // 0=NONE, 1=MIN_MODEM, 2=MAX_MODEM
```
The value is persisted in NVS (`wifi_sleep`) and exposed through
`GET/POST /api/settings/energy` and Settings → Energy. It is applied at
association, so changing it restarts the device.

**Timeline of attempts:**
1. **Feb 28 (e9c01ba)**: `MAX_MODEM` → `MIN_MODEM` ("less strict Wifi powersave")
2. **Mar 28 (c6859fd)**: `MAX_MODEM` → `NONE` ("fixing wifi issues")
3. **Sep 2026**: the value became configurable; the default remains `0` (`NONE`)
4. **Nov 2026 Phase 4 test unit**: stored `MAX_MODEM` with TX power 8 dBm and
   stayed associated through the DFS experiment

**The question:** Is WiFi stability still an issue, or was the Mar 28 change a
fix for a transient problem?

**Options:**
- **A) Keep `NONE`** — safest, highest idle current
- **B) Run a declared `MIN_MODEM` observation window** (1–2 weeks) and record
  disconnects; this is Phase 4 task 4.3 and has **not** been run
- **C) Adaptive approach** — use `MIN_MODEM` normally, fall back to `NONE`
  after reconnect failures (not implemented)

**Risk:** dropouts if the original instability is still present.

**Impact:** the −30–50 mA figure remains an estimate until the observation
window completes; no current measurement exists for this build.

---

### 2. Power Management — IDF only, light sleep reverted
**Location:** `src/main.cpp` (`configurePowerManagement()`),
`sdkconfig.defaults`

The Arduino environment cannot enable PM (`CONFIG_PM_ENABLE` is off in the
precompiled SDK). The IDF environment now does:

```text
CONFIG_PM_ENABLE=y
CONFIG_PM_PROFILING=y
```

`setup()` calls `esp_pm_configure()` with `max_freq_mhz=80`,
`min_freq_mhz=40`, `light_sleep_enable=false`. The PM counters in
`Network::logDiagnostics()` showed `APB_MIN` at 40 MHz for 54% of a 213 s
window; the WiFi APB lock was held for about 3% and `rtos0` for about 4%, with
the remainder unattributed before the first mode switch. That is **kept**.

Automatic light sleep (`CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`,
`light_sleep_enable=true`) was tried separately and **reverted**: the USB CDC
console stopped delivering bytes, HTTP/MQTT stayed alive, and the next boot
reported a Network-task panic (`heap_caps_malloc_prefer`). Recovery needed a
1200 bps touch during boot. See
`docs/BUILD_PARITY_BASELINE.md` §"Phase 4 power-management experiments".

**Status:** DFS kept; light sleep off. The old "leave it commented out" advice
applies to the Arduino environment only.

---

## High Priority Improvements (Priority: 🟡 MEDIUM)

### 3. Adaptive WiFi TX Power (not implemented)
**Location:** `src/network/WifiStation.cpp` (`applyEnergyConfig()`)

TX power is a fixed, persisted value (`Constants::DEFAULT_WIFI_POWER = 52`,
13 dBm). Before that default was corrected, the constant was `68` (**17 dBm**),
so devices that never saved energy settings were transmitting 4 dB hotter than
documented. The current settings API accepts `{8, 34, 52, 68, 80}`; the Phase 4
test unit stores `8`.

RSSI-based adaptation is **not implemented**. The sketch below is a proposal
only; if it is adopted it needs a spec delta and an explicit hysteresis/backoff
rule so it does not oscillate at the RSSI thresholds.

**Proposal sketch:**
```cpp
// In Network::task() main loop, around line 373-381 where WiFi status is checked:

static unsigned long lastTxPowerUpdate = 0;
static constexpr unsigned long TX_POWER_UPDATE_INTERVAL_MS = 60000; // Check every 60s

if (now - lastTxPowerUpdate >= TX_POWER_UPDATE_INTERVAL_MS && isConnected) {
    lastTxPowerUpdate = now;

    int32_t rssi = WiFi.RSSI();  // Signal strength in dBm (-50 to -100)
    wifi_power_t newPower = (wifi_power_t)energyConfig.wifi_power;

    if (rssi > -60) {
        // Very close to AP - use minimum power
        newPower = WIFI_POWER_7dBm;      // 40 mA
    } else if (rssi > -70) {
        newPower = WIFI_POWER_11dBm;     // 60 mA
    } else if (rssi > -80) {
        newPower = WIFI_POWER_13dBm;     // 80 mA
    } else {
        newPower = WIFI_POWER_17dBm;     // 100 mA
    }

    if (newPower != WiFi.getTxPower()) {
        WiFi.setTxPower(newPower);
        ESP_LOGD(TAG, "WiFi TX power adjusted: RSSI=%d dBm, TxPower=%d", rssi, newPower);
    }
}
```

**Impact:** 🟢 -5-20 mA depending on AP distance

---

### 4. Sensor Polling Intervals (implemented as a constant)
**Location:** `src/SensorController.h`, `src/task/SensorMonitor.cpp`

The system read interval is now a compile-time constant of **15 s**
(`SensorController::MEASUREMENT_INTERVAL_MS = 15000`), not a 1 Hz loop and not a
persisted setting. `SGP40` opts into a 1 Hz requirement
(`requiredIntervalMs() == 1000`) because its VOC-index algorithm assumes that
rate. The Monitor task ticks at the shortest configured requirement.

**Recommendations by sensor type:**
| Sensor | Current behaviour | Note |
|--------|-------------------|------|
| Temperature/Humidity (SHT4x) | 15 s shared phase | Slow change rate |
| BME680 (gas) | 15 s shared phase | 300 ms+ to read |
| CO₂ (SCD4x) | 15 s shared phase | Very slow change |
| PM2.5 | 15 s shared phase | Cumulative measurement |
| Light sensors | 15 s shared phase | Fast sensors |
| SGP40 | 1 s | VOC algorithm requirement |

Do not add a per-sensor interval to `EnergyConfig`; the spec deliberately makes
the system interval a constant so readings stay time-coherent.

**Impact:** not measured; it removes the old 1 Hz default.

---

### 5. NTP Update Frequency
**Location:** `src/network/NtpSync.h` (`UPDATE_INTERVAL_S`)

Updates every 3600 seconds (1 hour). This is reasonable and unchanged.

**Recommendation:** 12–24 hours only for battery devices; NTP uses ~50 ms of
WiFi activity per update.
```cpp
// Current: 3600 s (1 hour) — keep
// For battery devices, consider:
static constexpr uint32_t UPDATE_INTERVAL_S = 86400;  // 24 hours
```

**Impact:** <1 mA (NTP is brief); not measured.

---

## Low Priority Optimizations (Priority: 🟢 LOW)

### 6. LED Dark Mode (implemented)
**Location:** `src/StatusLed.cpp`, `src/DarkModeStatusLed.cpp`,
`EnergyConfig::led_dark_after_s`

The status NeoPixel goes dark after a configurable period of normal operation
(default 5 minutes, `0` = never). The MQTT publish flash is suppressed too;
`STARTUP` and `ERROR` always render, and a WiFi reconnect re-lights the LED for
another period. Once the black frame is latched, the wrapper also cuts the
NeoPixel supply rail (`setPowerRail(false)`), so the pixel's quiescent current
is removed; it is re-powered and re-rendered on the next visible state.
Configure it in Settings → Energy ("LED dark mode") or via
`POST /api/settings/energy {"led_dark_after_s": N}`; it applies live without a
restart.

**Impact:** 1–3 mA estimate (LED current plus one fewer NeoPixel write per
second); not measured. The main benefit is a dark room.

---

### 7. WebServer Optimization
Running ESPAsyncWebServer on core 1 continuously. No change made; with DFS the
CPU drops to 40 MHz between transactions, so HTTP latency should be re-measured
before adding `-O3` pragmas or changing queue depth.

**Impact:** not measured.

---

## Implementation Checklist

### ✅ Phase 1 - Quick Wins
- [ ] Run the declared `WIFI_PS_MIN_MODEM` observation window (Phase 4.3)
  - Stored per device via `POST /api/settings/energy {"wifi_sleep_mode": 1}`
  - Monitor for 1-2 weeks for disconnects
  - Estimated −30–50 mA if stable (not measured)
- [x] Fix the 15 s sensor read interval and SGP40's 1 Hz requirement
  - `SensorController::MEASUREMENT_INTERVAL_MS`, `requiredIntervalMs()`

### ✅ Phase 2 - Configuration Improvement
- [x] Add `GET/POST /api/settings/energy` for TX power, sleep mode and LED dark mode
- [x] Add the energy fields to the web settings UI
- [x] Show WiFi RSSI in `/api/status` and the About page

### ✅ Phase 3 - Enhancement (optional)
- [ ] Add adaptive WiFi TX power based on RSSI (needs a spec delta)
- [ ] Implement automatic low-power sensor mode when WiFi disconnects
- [ ] Add power consumption estimation to the web UI

### ✅ Phase 4 - Capability work (IDF environment only)
- [x] `CONFIG_PM_ENABLE=y` + `esp_pm_configure()` DFS 80/40 MHz — kept (4.1)
- [x] Automatic light sleep experiment — tested and reverted (4.2)
- [ ] WiFi power-save observation window (4.3)
- [ ] Brownout-threshold decision (4.4)
- [x] WiFi/lwIP buffer tuning (4.5)
- [ ] Re-run the display brownout observation on the IDF build

---

## Testing & Verification

### Measure Power Before & After

**Equipment needed:**
- USB power meter (measure mA)
- Multimeter
- Or measure voltage drop across inline resistor (0.1Ω, measure mV, calculate mA)

**Planning scenarios (NOT measured; replace with meter readings):**
```
1. Idle, WiFi connected (most common state)
   Before: ~120-150 mA
   After Phase 1: ~40-60 mA
   After Phase 2: ~30-50 mA

2. MQTT publishing every 15s
   Before: ~150-250 mA average
   After Phase 1+2: ~80-120 mA average

3. WiFi disconnected
   Before: ~80-100 mA (LED, CPU, sensors)
   After Phase 2: ~20-30 mA
```

### Stability Testing
1. Run overnight (8h minimum) in Phase 1+2 config
2. Monitor MQTT delivery (should be 100%)
3. Check for WiFi disconnects (should be 0)
4. Monitor heap fragmentation (should be stable)

---

## ESP32-S2 Power Characteristics

**Typical current draw by component:**
- CPU 80 MHz: ~80 mA
- CPU 40 MHz: ~50 mA  
- CPU 10 MHz: ~30 mA
- WiFi transmit: +50-100 mA
- WiFi receive/idle: +10-20 mA (with PS enabled)
- Sensor reads: +5-10 mA each
- NeoPixel (white @ max): +40 mA
- Light sleep: ~1/5 normal CPU power

**WiFi sleep modes:**
- `WIFI_PS_NONE`: No power save (default; safest)
- `WIFI_PS_MIN_MODEM`: Sleep between beacons (recommended for the observation)
- `WIFI_PS_MAX_MODEM`: Aggressive; the Phase 4 test unit runs this

**CPU modes with DFS (IDF build):**
- `CPU_MAX`/`APB_MAX`: 80 MHz (WiFi/display/I2C transactions hold the APB lock)
- `APB_MIN`: 40 MHz (idle); measured 54% of a 213 s window
- `SLEEP`: automatic light sleep — tried, reverted (USB CDC console lost)

---

## References

- [ESP32 Power Management API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/system/power_management.html)
- [ESP32 WiFi Power Save Mode](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#power-save-mode)
- [Light Sleep Guidelines](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
- [PlatformIO ESP32 Power Tips](https://docs.platformio.org/en/latest/platforms/espressif32.html)

---

## Summary Table (measured on 2026-11-17 unless marked estimate)

| Change | Where | Effect | Status |
|--------|-------|--------|--------|
| DFS 80/40 MHz | `sdkconfig.defaults`, `main.cpp` | 54% of a 213 s window at 40 MHz | ✅ IDF only, kept |
| Automatic light sleep | `CONFIG_FREERTOS_USE_TICKLESS_IDLE` | USB CDC console lost; Network panic | ❌ IDF only, reverted |
| WiFi sleep mode | `WifiStation::applyEnergyConfig()` | configurable; observation pending | ⚠️ 4.3 open |
| TX power adaptation | not implemented | −5–20 mA estimate | ⏳ open |
| Sensor interval | `SensorController` | 15 s system phase (SGP40 1 Hz) | ✅ done |
| LED dark mode + rail off | `DarkModeStatusLed` | 1–3 mA estimate | ✅ done |
| NTP interval | `NtpSync` | 3600 s; <1 mA estimate | ✓ keep |

**No total mA saving is quoted**: idle and transmit current remain unmeasured
on this hardware. The old 20–40 mA estimate is not a result of the Phase 4
build.
