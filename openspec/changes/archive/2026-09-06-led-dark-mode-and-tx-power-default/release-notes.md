# Release notes (paste into the GitHub release body)

## Behaviour changes

- **Status LED dark mode (new, on by default).** Five minutes after the device
  connects and starts its normal measurement cycle, the status LED turns off,
  including the brief publish flash. Startup (blue) and error (red) indications
  still show, and the LED lights up again for five minutes after any WiFi
  reconnect. Change or disable this in **Settings → Energy → LED dark mode**;
  it applies immediately without a restart. API: `led_dark_after_s` on
  `GET/POST /api/settings/energy` (seconds, `0` = never dark).
- **Default WiFi TX power corrected to 13 dBm.** The built-in default was
  actually 17 dBm despite being documented as 13 dBm. Devices that have
  **never** saved energy settings will now transmit at 13 dBm after this
  update. If a device at the edge of WiFi coverage becomes unreliable, select
  **High (17 dBm)** under Settings → Energy. Devices with a saved TX power are
  unaffected.
- **Saving energy settings no longer always restarts.** The device restarts
  only when WiFi TX power or sleep mode actually changed.
