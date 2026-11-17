# Build Parity Baseline — Arduino Framework (pre-ESP-IDF)

Evidence for the `adopt-esp-idf-build` change (Phase 1). The IDF build is
compared against these values line by line once it is flashed.

## Provenance

| Field | Value |
|---|---|
| Date captured | 2026-11-17 |
| Device | Adafruit QT Py ESP32-S2, MAC `d4:f9:8d:66:10:b0`, device ID `6610B0` |
| Firmware | `v0.1.29-1-gd54e097` (commit `d54e097`, source identical to `799ec04`/`main`) |
| Platform | `espressif32@7.1.3` — Arduino core 2.0.17, ESP-IDF 4.4.7 |
| Build command | `pio run -e adafruit_qtpy_esp32s2` |
| Host | macOS, USB CDC console at 115200 |

The QT Py was running `v0.1.29-1-g022c37b` (branch `espressif32_8_migration`)
when this baseline started. It was reflashed with the HEAD Arduino build so the
parity comparison is against this change's own source; the NVS partition was
preserved. The branch build remains reproducible from `022c37b`.

## 1.1 Boot baseline

Captured over USB CDC after a software restart. The USB device re-enumerates on
reset, so the host must reopen the port; on macOS the first ~2 s can be lost.
The values below come from a run where the port reopened before `Started`, so
the whole boot sequence is present.

```text
00002005 I     main Started
00002005 I     main Reset reason: SW (3)
00002006 I     main Boot heap: internal free=140148 largest=110580, total free=2235251
00002007 W     main Core dump present but unreadable (err 0x104)
00002172 I     main Initializing I2C
00002174 I     main Sensor SHT4x added at 0x44
00002178 I   sensor Found 2 sensors total
00002179 I  control PID tuning from config: Kp=0.5000 Ki=0.00010 Kd=0.0 interval=60s
00002180 I  control Target temperature set to 19.0 C
00002182 I     main Task watchdog configured (30s timeout)
00002183 I      ota OTA workers ready (check 7168 B, update 12288 B static stacks)
00002184 I     main Timezone: UTC0
00002194 I     main Display disabled in config; Network::startAP() will still probe at AP-mode entry
00002196 I     main Starting sensor task
00002197 I    hwwdt RTC hardware watchdog enabled (60000 ms)
00002199 I   sensor SensorMonitor tick: 15000 ms
00002203 I      net Previous connection failures: 0
00002255 I      net NVS probe: used=178 free=452 total=630 ns=5
00002256 I      net NVS probe: phy cal_data len=1904 err=0x0
00002257 I      net Pre-WiFi heap: internal free=119988 largest=94196, total free=2215091
00002324 I      net WiFi config: TX Power=52, Sleep Mode=NONE
00002325 I      net Connecting to WiFi Thoth (attempt 1/3) ...
00002422 I      net WiFi event: STA_CONNECTED ch=11
00002947 I      net WiFi event: GOT_IP 192.168.110.252 rssi=-58
00003334 I      net WiFi connected, IP: 192.168.110.252
00003338 I      net mDNS responder started: klima-6610b0.local
00003421 I      net mDNS instance name: 'Klima Test'
00003423 I      net Starting NTP...
00003477 I      net NTP time: 14:01:28
00003481 I     mqtt Server set to hostname mqtt.home.wuerl.net:1883
00003482 I     mqtt TX buffer: 2048 bytes (requested 2048, degraded=0)
00003485 I      net Switching webserver to OPERATIONAL mode...
00003493 I     http Webserver started on port 80 (mode=2)
00003494 I      net Webserver started - system ready, free heap: 47896 bytes
00005524 I     mqtt Connected to mqtt.home.wuerl.net:1883
00005535 I      net HA discovery published (1526 B)
```

| Measurement | Value |
|---|---|
| Reset reason | `SW` (3) — the restart was requested through `POST /api/restart` |
| Boot internal heap free | 140148 B |
| Boot internal largest block | 110580 B |
| Boot total (malloc) free | 2235251 B |
| Pre-WiFi internal heap | free 119988 B, largest 94196 B |
| Runtime internal heap (`/api/diag/requests`, first `/api/status` after boot) | 41788 B |
| Runtime internal largest block | 17396 B |
| `/api/status` `largest_free_block` | 2064372 B (PSRAM, `MALLOC_CAP_8BIT` — not internal SRAM) |
| `/api/about` `free_heap` / `heap_size` / `min_free_heap` | 40980 / 160900 / 32092 B |
| Network task stack HWM | 3132 B free (8192 B stack) |
| SensorMonitor task stack HWM | 4112 B free (6144 B stack) |
| Runtime heap diagnostic | `heap=41552 bytes (min=32092)` at uptime 903 s |
| Network loop stats at the same point | count=898, avg=2 ms, min=0 ms, max=48 ms |

The boot line also reports a stale core-dump partition entry:
`Core dump present but unreadable (err 0x104)`. That is the baseline behaviour
of the flash core-dump path on this unit, not a clean "no dump" boot.

## 1.2 Firmware size and partition usage

```text
Advanced Memory Usage is available via "PlatformIO Home > Project Inspect"
RAM:   [===       ]  25.0% (used 82020 bytes from 327680 bytes)
Flash: [========  ]  75.3% (used 1430746 bytes from 1900544 bytes)
```

| Measurement | Value |
|---|---|
| Flash used (ELF segments) | 1,430,746 B |
| `firmware.bin` on disk | 1,431,104 B |
| App partition (`app0`/`app1`, `partitions.csv`) | 1,900,544 B (1856 KiB) |
| Partition usage | 75.3% |
| Cap check | 1,430,746 B = 1397 KiB, below the 1900 KB cap |
| Static RAM used | 82,020 B of 327,680 B (25.0%) |

## 1.3 Configuration round-trip

Snapshot A was taken from the running HEAD build, the device was restarted with
`POST /api/restart`, and snapshot B was taken after WiFi and the web server came
back. The config-bearing fields were compared after removing live telemetry
(uptime, sensor values, MQTT counters, control output).

| Endpoint | Compared fields | Result |
|---|---|---|
| `/api/status` | `device_name`, `elevation`, `target_temperature`, `control_enabled`, `wifi_ssid` | unchanged |
| `/api/settings/energy` | `wifi_power`, `wifi_sleep_mode`, `led_dark_after_s` | unchanged |
| `/api/sensors/config` | sensor assignments (SHT4x at 0x44) | unchanged |
| `/api/mqtt` | `enabled`, `host`, `port`, `username`, `prefix`, `interval`, `ha_discovery`, `buffer_size` | unchanged |
| `/api/display` | `enabled`, `rotation`, `interval`, `warn_frost_c`, `warn_humidity_pct` | unchanged |
| `/api/syslog` | `enabled`, `host`, `port` | unchanged |
| `/api/settings/timezone` | `timezone` (`UTC0`) | unchanged |
| `/api/control` | `setpoint`, `output_min`, `output_max`, `kp`, `ki`, `kd`, `control_interval_s` | unchanged |
| `/api/actuator` | `host`, `channel`, `cycle_s`, `travel_s`, `safety_max_c`, `safety_shutoff` | unchanged |

## 1.4 NVS layout

Namespace: `klima`. All writes go through `Config::ConfigManager`; there are no
other NVS writers in `src/`. Types below are the on-flash NVS types:

- `putBool` → `U8` (0x01)
- `putUChar` → `U8`
- `putChar` → `I8` (0x11)
- `putUShort` → `U16` (0x02)
- `putString` → `SZ` (0x21, variable-length)
- `putFloat` → `BLOB` (0x42) with a 4-byte payload and a `BLOB_IDX` entry

### Complete key map written by `ConfigManager`

| Key | Type | Writer |
|---|---|---|
| `wifi_ssid` | SZ | `saveWiFiConfig` |
| `wifi_pass` | SZ | `saveWiFiConfig` |
| `configured` | U8 (bool) | `saveWiFiConfig` / `markUnconfigured` |
| `wifi_failures` | U8 | `incrementConnectionFailures` / `resetConnectionFailures` |
| `device_name` | SZ | `saveDeviceConfig` / `updateDeviceName` |
| `target_temp` | BLOB, 4 B float | `saveDeviceConfig` / `updateTargetTemperature` |
| `ctrl_enabled` | U8 (bool) | `saveDeviceConfig` / control enable |
| `elevation` | BLOB, 4 B float | `saveDeviceConfig` / `updateElevation` |
| `sensor_i2c` | U8 | `saveDeviceConfig` / sensor address update |
| `timezone` | SZ | `saveDeviceConfig` / timezone update |
| `act_host` | SZ | `saveDeviceConfig` / actuator host update |
| `act_ch` | I8 | `saveDeviceConfig` / actuator channel update |
| `tpo_cycle` | U16 | `saveDeviceConfig` / actuator tuning |
| `tpo_travel` | U16 | `saveDeviceConfig` / actuator tuning |
| `safe_max` | BLOB, 4 B float | `saveDeviceConfig` / actuator tuning |
| `safe_hyst` | BLOB, 4 B float | `saveDeviceConfig` / actuator tuning |
| `pid_kp` | BLOB, 4 B float | `saveDeviceConfig` / PID gains |
| `pid_ki` | BLOB, 4 B float | `saveDeviceConfig` / PID gains |
| `pid_kd` | BLOB, 4 B float | `saveDeviceConfig` / PID gains |
| `ctrl_intv` | U16 | `saveDeviceConfig` / PID gains |
| `sns_assign` | SZ | `saveSensorConfig` |
| `mqtt_enabled` | U8 (bool) | `saveMqttConfig` |
| `mqtt_host` | SZ | `saveMqttConfig` |
| `mqtt_port` | U16 | `saveMqttConfig` |
| `mqtt_user` | SZ | `saveMqttConfig` |
| `mqtt_pass` | SZ | `saveMqttConfig` |
| `mqtt_prefix` | SZ | `saveMqttConfig` |
| `mqtt_interval` | U16 | `saveMqttConfig` |
| `mqtt_ha_disc` | U8 (bool) | `saveMqttConfig` |
| `energy_wifi_pw` | U8 | `saveEnergyConfig` |
| `wifi_sleep` | U8 | `saveEnergyConfig` |
| `led_dark_s` | U16 | `saveEnergyConfig` |
| `syslog_enabled` | U8 (bool) | `saveSyslogConfig` |
| `syslog_host` | SZ | `saveSyslogConfig` |
| `syslog_port` | U16 | `saveSyslogConfig` |
| `disp_enabled` | U8 (bool) | `saveDisplayConfig` |
| `disp_rot` | U8 | `saveDisplayConfig` |
| `disp_intv` | U16 | `saveDisplayConfig` |
| `warn_frost` | BLOB, 4 B float | `saveDisplayConfig` |
| `warn_hum` | U8 | `saveDisplayConfig` |

### Deployed entries on this unit

A raw read of the `nvs` partition (`0x9000`, 20 KiB) shows these entries in
namespace `klima` (index 5). The dump was taken before the HEAD Arduino
reflash and again after it; both reads contained the same entries, confirming
the partition survived the reflash. Entries not listed still hold their
compiled-in defaults and have never been written, so they are absent from
flash; they are part of the map above and must keep round-tripping when
written.

| Key | On-flash type | Value |
|---|---|---|
| `configured` | U8 | `1` |
| `wifi_ssid` | SZ | `Thoth` |
| `wifi_pass` | SZ | (stored) |
| `wifi_failures` | U8 | `0` |
| `device_name` | SZ | `Test` |
| `target_temp` | BLOB_IDX → BLOB | 4 B float (`19.0`) |
| `ctrl_enabled` | U8 | `0` |
| `sns_assign` | SZ | `68=SHT4x` |
| `mqtt_enabled` | U8 | `1` |
| `mqtt_host` | SZ | `mqtt.home.wuerl.net` |
| `mqtt_port` | U16 | `1883` |
| `mqtt_user` | SZ | (empty) |
| `mqtt_pass` | SZ | (empty) |
| `mqtt_prefix` | SZ | `sensors_test/Test` |
| `mqtt_interval` | U16 | `15` |
| `mqtt_ha_disc` | U8 | `1` |

Namespace indices observed in the partition: `misc` (1), `nvs.net80211` (2),
`phy` (3), `dhcp_state` (4), `klima` (5). NVS stats from the boot log:
used=178, free=452, total=630 entries, 5 namespaces.

## 1.5 Current draw and display cadence

No power meter or current shunt is connected to the capture host (2026-11-17),
so idle and transmit current were **not measured** on this build. The only
figures available are the planning estimates in `POWER_OPTIMIZATION.md`, which
are dated March 2026 and are not attributable to `v0.1.29-1-gd54e097`:

| State | Documented estimate | Status |
|---|---|---|
| Idle | ~80–120 mA (WiFi sleep disabled, ~100 mA baseline) | unmeasured planning figure |
| Transmitting MQTT | ~200–250 mA | unmeasured planning figure |

Phase 4 must replace both with meter readings taken on the same unit and
recorded against the build under test.

Display refresh cadence is known and attributable: the display is disabled on
this unit (`/api/display`: `enabled=false`, `interval=60`), so it performs no
refreshes. The baseline cadence is therefore "none"; any later display-enabled
observation must record its own cadence.

## 1.6 Brownout observation

Decision (2026-11-17): `assess-display-brownout-risk` is **consciously
deferred**. It will be re-run on the IDF build after Phase 2 parity and before
any Phase 4 power-management experiments. The observation is not straddling the
build change because it has not started on the Arduino build. The deferral is
also recorded in that change's `tasks.md`.

## Open items

- Idle and transmit current remain unmeasured; only documented planning
  estimates exist.
- The stale core-dump partition entry (`err 0x104`) needs a deliberate
  panic test in Phase 3 (task 3.10) rather than being treated as clean.

## Phase 2 build verification (ESP-IDF environment)

Environment `adafruit_qtpy_esp32s2_idf` uses `framework = espidf, arduino` on
`espressif32@7.1.3` (IDF 4.4.7, Arduino core 2.0.17). The Arduino environment
above still builds unchanged (1,430,746 B flash).

| Measurement | IDF build |
|---|---|
| Flash used (ELF segments) | 1,780,842 B (1729 KiB) |
| `firmware.bin` on disk | 1,781,200 B |
| App partition | 1,900,544 B (1856 KiB) |
| Partition usage | 93.7% |
| Cap check | 1,780,842 B, below the 1900 KB cap |
| Static RAM used | 69,556 B of 327,680 B (21.2%) |
| Native tests | `pio test -e native`: 722/722 passed |
| **Final build after `-Os` + WiFi buffer tuning** | **1,514,286 B flash (79.7%), 69,092 B RAM**; `firmware.bin` 1,514,656 B |

Generated `sdkconfig.adafruit_qtpy_esp32s2_idf` checks:

| Setting | Generated value |
|---|---|
| `CONFIG_ARDUINO_VARIANT` | `"adafruit_qtpy_esp32s2"` |
| `CONFIG_AUTOSTART_ARDUINO` | `y` |
| `CONFIG_SPIRAM_USE_MALLOC` | `y` |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | `0` |
| `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` | `y` |
| `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` | `y` |
| `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF` | `y` |
| `CONFIG_TINYUSB_ENABLED` / `CONFIG_TINYUSB_CDC_ENABLED` | `y` / `y` |
| `CONFIG_ESP32S2_DEFAULT_CPU_FREQ_MHZ` | `80` |
| `CONFIG_ESPTOOLPY_FLASHSIZE` / `FLASHFREQ` | `"4MB"` / `"80m"` |
| `CONFIG_PARTITION_TABLE_CUSTOM` | `y` (`partitions.csv`) |
| `CONFIG_MBEDTLS_PSK_MODES` | `y` (needed by Arduino's WiFiClientSecure source) |

## Phase 3 hardware parity results (ESP-IDF build)

The clean IDF build (`v0.1.29-1-gd54e097`, 1,788,992 B) was flashed and
power-cycled. USB CDC enumerated and the boot log was readable:

```text
00002305 I     main Started
00002306 I     main Reset reason: SW (3)
00002306 I     main Boot heap: internal free=125816 largest=98304, total free=2220931
00002307 W     main Core dump present but unreadable (err 0x104)
00002472 I     main Initializing I2C
00002474 I     main Sensor SHT4x added at 0x44
00002477 I   sensor Successfully initialized sensor SHT4x
00002478 I   sensor Found 2 sensors total
00002479 I  control PID tuning from config: Kp=0.5000 Ki=0.00010 Kd=0.0 interval=60s
00002482 I     main Task watchdog configured (30s timeout)
00002483 I      ota OTA workers ready (check 7168 B, update 12288 B static stacks)
00002494 I     main Display disabled in config; Network::startAP() will still probe at AP-mode entry
00002557 I      net NVS probe: used=178 free=452 total=630 ns=5
00002559 I      net Pre-WiFi heap: internal free=106008 largest=81920, total free=2201123
00002725 I      net Connecting to WiFi Thoth (attempt 1/3) ...
00002819 I      net WiFi event: STA_CONNECTED ch=11
00004324 I      net WiFi event: GOT_IP 192.168.110.252 rssi=-71
00004729 I      net WiFi connected, IP: 192.168.110.252
00004733 I      net mDNS responder started: klima-6610b0.local
00004816 I      net mDNS instance name: 'Klima Test'
00004847 I      net NTP time: 15:16:41
00004853 I     mqtt Initialized (enabled=1, host=mqtt.home.wuerl.net, prefix=sensors_test/Test)
00004857 I     http Webserver started on port 80 (mode=2)
```

| Check | Baseline (Arduino) | IDF build | Result |
|---|---|---|---|
| `Reset reason` | `SW (3)` | `SW (3)` | match |
| Boot internal heap free | 140148 B | 125816 B | −14,332 B |
| Boot internal largest block | 110580 B | 98304 B | −12,276 B |
| Boot total free | 2235251 B | 2220931 B | −14,320 B |
| Pre-WiFi internal heap | 119988 / 94196 B | 106008 / 81920 B | −13,980 / −12,276 B |
| Runtime `/api/about` `free_heap` | 40980 B | 33652 B | −7,328 B |
| Runtime `/api/about` `heap_size` | 160900 B | 151760 B | −9,140 B |
| Runtime internal heap (`/api/diag/requests`) | 41788 B | 31940 B | −9,848 B |
| Runtime internal largest block | 17396 B | 14336 B | −3,060 B |
| Runtime `min_free_heap` | 32092 B | 18612 B | −13,480 B |
| Network task stack HWM | 3132 B free / 8192 B | 3336 B free / 8192 B | IDF uses 204 B less |
| SensorMonitor stack HWM | 4112 B free / 6144 B | 3348 B free / 6144 B | IDF uses 764 B more |
| 15-minute heap diagnostic | heap=41552 (min=32092) | heap=34804 (min=17564) | −6,748 / −14,528 B |
| Network loop stats | count=898, avg=2, max=48 ms | count=898, avg=3, max=48 ms | match |
| PSRAM detected | 2094719 B | 2095019 B | match (within 300 B) |
| WiFi SSID / IP | Thoth / 192.168.110.252 | Thoth / 192.168.110.252 | match |
| mDNS hostname | `klima-6610b0.local` | `klima-6610b0.local` | match |
| NTP local time | plausible | plausible (15:16:41) | match |
| Configuration round-trip | unchanged | unchanged | match (all endpoints) |
| Sensors | SHT4x + ESP32 online | SHT4x + ESP32 online | match |
| MQTT / HA discovery | connected, discovery published | connected, discovery published (1,498 B) | match; discovery topic present |
| HTTP/UI | 200 | `/api/status` 200, `/` 200 (8,558 B) | match |
| CSRF | header required | POST without header 403, with header 200 | match |
| OTA check/update/rollback | n/a | `/api/ota/check` reachable; OTA update and rollback-confirm verified on the test device | match; published asset remains `firmware.bin` |
| Display | disabled | display and LED verified working after board-variant init fix (`initVariant` must drive `NEOPIXEL_POWER`) | match |
| Flash core dump | stale/unreadable (`err 0x104`) | deliberate panic produced a readable summary: `task='loopTask' PC=0x40026232 cause=29` with 6-frame backtrace | match in kind; summary path works |

The IDF build uses ~14 KB more internal SRAM at boot and ~7 KB less at
runtime than the Arduino build. The 15-minute sample shows `min_free_heap`
= 17564 B, only ~1.2 KB above the 16384 B low-heap restart threshold; Phase 4.5
must decide whether the WiFi/lwIP buffer or heap settings need tuning. The IDF
HWM measurements are now quoted in the `Network::startTask()` and
`SensorMonitor::startTask()` comments; the existing 8192 B / 6144 B stacks still
meet their minimum sizes.

### 4.5 WiFi/lwIP buffer tuning

The Phase 3 measurement showed the IDF build’s internal heap was ~14 KB lower at
boot and its `min_free_heap` (17,564 B) was only ~1.2 KB above the 16,384 B
low-heap restart threshold. Diffing the generated `sdkconfig` against the
precompiled Arduino SDK showed the IDF defaults used 16 static RX/TX WiFi
buffers instead of 8, and enabled WiFi/RX code placement in IRAM.

The tuned `sdkconfig.defaults` sets:

| Setting | Value |
|---|---|
| `CONFIG_ESP32_WIFI_STATIC_RX_BUFFER_NUM` | `8` |
| `CONFIG_ESP32_WIFI_STATIC_TX_BUFFER_NUM` | `8` |
| `CONFIG_ESP32_WIFI_CACHE_TX_BUFFER_NUM` | `16` |
| `CONFIG_ESP32_WIFI_RX_IRAM_OPT` | `n` |
| `CONFIG_ESP32_WIFI_IRAM_OPT` | `n` |

After a clean rebuild and flash, WiFi still associates and serves HTTP, MQTT is
connected, and `/api/about` reports:

| Measurement | Arduino baseline | IDF after tuning |
|---|---|---|
| `free_heap` | 40980 B | 62292 B |
| `heap_size` | 160900 B | 180004 B |
| `min_free_heap` | 32092 B | 57156 B |
| `/api/status` `largest_free_block` | 2064372 B | 2064384 B |
| `/api/diag/requests` internal largest block | 17396 B | 30720 B |

The final IDF build also sets `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` and
`CONFIG_COMPILER_STACK_CHECK_MODE_NORM=y`, matching the precompiled Arduino
SDK's `-Os` build and stack protector. That reduced flash from 1,788,992 B to
1,514,656 B (274 KB, 93.7% → 79.7% of the app partition) and raised runtime
`free_heap`/`min_free_heap` further.

The low-heap guard threshold is unchanged at 16,384 B and is no longer close to
the observed minimum. `pio test -e native` remains green.



### Early bisect note

Early attempts that appeared to block before USB CDC were traced to stale
incremental objects in `.pio/build/adafruit_qtpy_esp32s2_idf` after temporary
diagnostic source edits. A clean rebuild booted normally. No source workaround
was kept; the shipped application code boots unchanged under IDF once the build
directory is clean.
