# KlimaControl Power Optimization Guide

---

## 🎯 TL;DR - What to Do

**Your findings are correct: ESP-IDF power management (`esp_pm_configure`) does NOT work effectively in Arduino framework.**

Based on your git history, you've already learned:
1. Light sleep + power management doesn't work in Arduino (you tried Mar 16, removed Mar 20)
2. WiFi power save causes stability issues (you disabled it Mar 28 to fix restarts)
3. The only reliable power saving in Arduino is higher-level changes

### Recommended approach:
```
✅ SAFE, LOW RISK:
   - Increase sensor polling interval: 1s → 5s (save 5-15 mA)
   - Reduce WiFi RSSI checks (already at reasonable 1s intervals)
   - These won't break anything

⚠️ MEDIUM RISK (OPTIONAL):
   - Re-test WIFI_PS_MIN_MODEM: could save 30-50 mA but caused issues before
   - Only worth trying if current WiFi is rock-solid
   - Requires 1-2 weeks of real-world testing

❌ DO NOT BOTHER:
   - esp_pm_configure() - Serial logging prevents light sleep from ever working
   - Dynamic frequency scaling - breaks WiFi timing
   - Anything requiring bare metal ESP-IDF changes
```

**Realistic power savings from Arduino optimizations: 20-40 mA (20-35% reduction)**, not 60%.

---

## Executive Summary

**Important:** You've already disabled WiFi power save (Mar 28, 2026) to fix WiFi stability. Power management API doesn't work in Arduino framework. Realistic optimizations in Arduino are more limited than ESP-IDF projects.

### Estimated Current Power Draw
- **Active state (transmitting MQTT):** ~200-250 mA
- **Idle state:** ~80-120 mA (WiFi sleep disabled, ~100 mA baseline)
- **With WIFI_PS_MIN_MODEM:** ~30-60 mA (IF WiFi stays stable)

### Realistic Optimizations (Arduino Framework)
- **Re-enable WiFi sleep:** -30-50 mA (if stable) — **RISKY, requires 1-2 week test**
- **Increase sensor polling:** -5-15 mA (proven safe)
- **Adaptive TX power:** -5-20 mA (low risk)

**Total realistic savings: 20-40 mA (20-35% reduction in idle state)**  
*Not 60% - that would require bare metal ESP-IDF, not Arduino framework*

---

## ⚠️ IMPORTANT: Arduino Framework Limitations

Previous testing (your git history from Feb-Mar 2026) showed that **ESP-IDF power management does NOT work reliably in Arduino framework**:
- ❌ Light sleep is blocked by Serial logging and ISR activity
- ❌ Dynamic frequency scaling breaks WiFi/I2C timing
- ❌ Power management API works inconsistently

**Don't waste time on `esp_pm_configure()`** - focus on proven approaches below.

---

## Critical Issues to Fix (Priority: 🔴 HIGH)

### 1. WiFi Sleep Mode Disabled for Stability
**Location:** `src/Network.cpp:128`

**Current code:**
```cpp
WiFi.setSleep(WIFI_PS_NONE);  // Disabled for WiFi stability (Mar 28 commit c6859fd)
```

**Timeline of attempts:**
1. **Feb 28 (e9c01ba)**: Changed from `MAX_MODEM` → `MIN_MODEM` ("less strict Wifi powersave")  
2. **Mar 28 (c6859fd)**: Changed from `MAX_MODEM` → `NONE` ("fixing wifi issues")
   - Commit message: "Disable WiFi power save for best reception"
   - Explicitly chose stability over power consumption

**The question:** Is WiFi stability still an issue, or was this a fix for a transient problem?

**Options:**
- **A) Keep WIFI_PS_NONE** - If current WiFi is stable, leave it as-is
- **B) Re-test WIFI_PS_MIN_MODEM** - If you can afford to test 1-2 weeks with `MIN_MODEM`, the power savings are worth it
- **C) Adaptive approach** - Use `MIN_MODEM` normally, fall back to `NONE` after reconnect failures

**Recommended:** Check if the WiFi stability issue (whatever caused the Mar 28 change) is still present. If your WiFi is stable now, try:
```cpp
WiFi.setSleep(WIFI_PS_MIN_MODEM);  // -30-50 mA if it stays stable
```

**Risk:** May cause WiFi dropouts if original issue is still present

**Impact:** 🟢 -30-50 mA in idle state (if WiFi remains stable)

---

### 2. Power Management - SKIP THIS
**Location:** `src/main.cpp:116-121`

❌ **Don't enable this.** Your previous testing already proved it doesn't work:
- Commit `dbc5f81` (Mar 16): "try to enable light sleep" - added it
- Commit `d889b3b` (Mar 20): "remove frequency setting" - removed it 9 days later

**The issue:** Arduino framework's Serial logging + WiFi ISRs prevent light sleep from ever triggering. You need a bare metal ESP-IDF project for this to work.

**Status:** Leave commented out as-is ✅

---

## High Priority Improvements (Priority: 🟡 MEDIUM)

### 3. Adaptive WiFi TX Power
**Location:** `src/Network.cpp:130-132`

Currently uses fixed 13 dBm. Closer to AP = use less power.

**Add RSSI-based TX power adaptation:**
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

### 4. Optimize Sensor Polling Intervals
**Location:** `src/task/SensorMonitor.h:23` and API configuration

Default is 1000 ms (1 Hz sampling). This is high for environmental monitoring.

**Recommendations by sensor type:**
| Sensor | Recommended Interval | Note |
|--------|-------------------|------|
| Temperature/Humidity (SHT4x) | 10-30s | Slow change rate |
| BME680 (gas) | 60-300s | Takes 300ms+ to read |
| CO₂ (SCD4x) | 60-600s | Very slow change |
| PM2.5 | 60-300s | Cumulative measurement |
| Light sensors | 5-60s | Fast sensors, use 30s default |

**Implementation:** Create an API endpoint to configure per-sensor intervals, or use a global multiplier.

**Quick fix - increase to 5 seconds:**
```cpp
// In Config.h, add to EnergyConfig struct:
struct EnergyConfig {
    uint8_t wifi_power = Constants::DEFAULT_WIFI_POWER;
    uint16_t sensor_poll_interval_ms = 5000;  // Default 5 second polling
    EnergyConfig() = default;
};

// Then in SensorMonitor::startTask() or via config API:
sensorMonitor.setReadingInterval(config.loadEnergyConfig().sensor_poll_interval_ms);
```

**Impact:** 🟢 -5-15 mA (depends on sensor count)

---

### 5. Reduce NTP Update Frequency
**Location:** `src/Network.cpp:394-415`

Currently updates every 3600 seconds (1 hour). This is reasonable, but could be adjusted.

**Recommendation:** 12-24 hours for stable WiFi. NTP uses only ~50ms of WiFi activity.
```cpp
// Current: 3600s (1 hour) ✓ This is already reasonable
// For battery devices, increase to:
static constexpr uint32_t NTP_UPDATE_INTERVAL_S = 86400;  // 24 hours
```

**Impact:** 🟢 <1 mA (minimal - NTP is brief)

---

## Low Priority Optimizations (Priority: 🟢 LOW)

### 6. LED Update Frequency
**Location:** `src/Network.cpp:356-358` and `src/StatusLed.cpp`

LED updates run every 1 second. Could be reduced to 100ms.

```cpp
// In Network::task() main loop:
static unsigned long lastLedUpdate = 0;
if (statusLed && (now - lastLedUpdate >= 100)) {  // Was 1000
    lastLedUpdate = now;
    statusLed->update();
}
```

**Impact:** <1 mA (NeoPixel itself is efficient)

---

### 7. WebServer Optimization
Running ESPAsyncWebServer on core 1 continuously.

**Considerations:**
- Web API latency might increase if CPU is at min_freq_mhz
- Can add `#pragma GCC optimize("O3")` to performance-critical paths
- Consider reducing request queue depth

**Impact:** Minimal with proper power mgmt in place

---

## Implementation Checklist

### ✅ Phase 1 - Quick Wins (1-2 hours)
- [ ] **OPTIONAL:** Test re-enabling `WIFI_PS_MIN_MODEM` if WiFi is now stable
  - Change line 128 in Network.cpp
  - Monitor for 1-2 weeks for disconnects
  - Potential: -30-50 mA if stable
- [ ] Add sensor polling interval to EnergyConfig (5-10 min)
  - Add `uint16_t sensor_poll_interval_ms = 5000` to `EnergyConfig` struct
  - Update Config.cpp to load/save it
  - Potential: -5-15 mA

### ✅ Phase 2 - Configuration Improvement (1-2 hours)
- [ ] Create API endpoints to configure energy settings
- [ ] Add sensor polling interval to web settings UI
- [ ] Add WiFi RSSI display to diagnostics

### ✅ Phase 3 - Enhancement (optional)
- [ ] Add adaptive WiFi TX power based on RSSI
  - Potential: -5-20 mA depending on AP proximity
- [ ] Implement automatic low-power sensor mode when WiFi disconnects
- [ ] Add power consumption estimation to web UI

### ✅ Phase 4 - Do NOT Implement
- ❌ `esp_pm_configure()` - Proven not to work in Arduino framework  
- ❌ Light sleep - Serial logging prevents this from ever working
- ❌ Dynamic frequency scaling - Breaks WiFi/I2C timing in Arduino

---

## Testing & Verification

### Measure Power Before & After

**Equipment needed:**
- USB power meter (measure mA)
- Multimeter
- Or measure voltage drop across inline resistor (0.1Ω, measure mV, calculate mA)

**Test scenarios:**
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
- `WIFI_PS_NONE`: No power save (current approach) ❌
- `WIFI_PS_MIN_MODEM`: Sleep between beacons (RECOMMENDED) ✅
- `WIFI_PS_MAX_MODEM`: Aggressive, may miss WiFi events

---

## References

- [ESP32 Power Management API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-reference/system/power_management.html)
- [ESP32 WiFi Power Save Mode](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#power-save-mode)
- [Light Sleep Guidelines](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/sleep_modes.html)
- [PlatformIO ESP32 Power Tips](https://docs.platformio.org/en/latest/platforms/espressif32.html)

---

## Summary Table (Arduino Framework Reality)

| Change | Code Lines | Est. Savings | Priority | Risk | Arduino Works? |
|--------|-----------|--------------|----------|------|-----------------|
| Re-enable WiFi sleep | Network.cpp:128 | -30-50 mA | MEDIUM | 🔴 HIGH | ⚠️ Maybe (caused issues before) |
| PM config | main.cpp:116 | None | SKIP | N/A | ❌ NO (Serial prevents sleep) |
| TX power adapt | Network.cpp:380+ | -5-20 mA | LOW | Low | ✅ YES |
| Sensor interval | Config.h:117 | -5-15 mA | MEDIUM | Low | ✅ YES |
| LED update | Network.cpp:357 | <1 mA | LOW | None | ✅ YES |
| NTP interval | Network.cpp:394 | <1 mA | LOW | None | ✅ YES |

**Total realistic savings: 20-40 mA (20-35% reduction in idle state)**

**DO NOT implement: PM config, light sleep, or frequency scaling — Arduino framework incompatible**
