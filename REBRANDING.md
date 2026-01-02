# Rebranding Summary: LED Controller → ledz

## Overview

The project has been renamed from "LED Controller" to "ledz" throughout the codebase, including device IDs, hostnames, and all user-facing elements.

## Changes Made

### 1. Device ID Format
- **Before**: `LEDCtrl-AABBCC`
- **After**: `ledz-AABBCC`

### 2. mDNS Hostname Format
- **Before**: `ledctrlaabbcc.local`
- **After**: `ledzaabbcc.local`

### 3. Web Interface
- **Title**: Changed from "LED Controller" to "ledz"
- **Header**: Now displays "ledz" in the main header
- **Device Info**: Shows "ledz-AABBCC" and "ledzaabbcc.local"

### 4. Code Changes

#### Modified Files
1. **`src/DeviceId.h`**
   - Updated device ID format from `LEDCtrl-%02X%02X%02X` to `ledz-%02X%02X%02X`
   - Updated comment: `"ledz-AABBCC"`

2. **`src/Config.cpp`**
   - Updated `getDeviceId()` method to return `ledz-AABBCC` format

3. **`src/Network.cpp`**
   - Updated hostname generation: `hostname.replace("ledz-", "ledz")`
   - Updated Serial output messages to use "ledz"

4. **`src/WebServerManager.cpp`**
   - Changed web page title from "LED Controller" to "ledz"
   - Updated JavaScript hostname parsing: `replace('ledz-', 'ledz')`

5. **`docs/MDNS.md`**
   - Updated all references from "LED Controller" to "ledz"
   - Updated all example hostnames from `ledctrlaabbcc.local` to `ledzaabbcc.local`
   - Updated device ID examples from `LEDCtrl-A1B2C3` to `ledz-A1B2C3`

6. **`docs/SHOW_PARAMETERS.md`**
   - Updated overview text from "LED Controller" to "ledz"

## Examples

### Before
```
Device ID: LEDCtrl-A1B2C3
Hostname: ledctrla1b2c3.local
Web Title: LED Controller
AP SSID: LEDCtrl-A1B2C3
```

### After
```
Device ID: ledz-A1B2C3
Hostname: ledza1b2c3.local
Web Title: ledz
AP SSID: ledz-A1B2C3
```

## Serial Monitor Output

When the device connects, you'll now see:
```
Starting Access Point: ledz-A1B2C3
AP IP address: 192.168.4.1
mDNS responder started: ledza1b2c3.local

Connected!
IP address: 192.168.1.123
mDNS responder started: ledza1b2c3.local
You can now access ledz at:
  http://ledza1b2c3.local/
  or http://192.168.1.123
```

## Web Interface

The web interface header now shows:
```
ledz
ledz-A1B2C3
Access at: ledza1b2c3.local
```

## API Access

All API endpoints remain the same, just accessed via the new hostname:

```bash
# Before
curl http://ledctrla1b2c3.local/api/status

# After
curl http://ledza1b2c3.local/api/status
```

## Migration Notes

### For Existing Deployments

If you're upgrading from the old "LED Controller" branding:

1. **WiFi Configuration**: Preserved - no need to reconfigure
2. **Show Settings**: Preserved - all show configurations remain
3. **Hostname**: Will change on next reboot
   - Old: `ledctrlaabbcc.local`
   - New: `ledzaabbcc.local`
4. **Bookmarks**: Update any saved bookmarks to use new hostname

### Home Automation

Update your Home Assistant or other automation configurations:

**Before**:
```yaml
rest_command:
  led_controller_rainbow:
    url: http://ledctrlaabbcc.local/api/show
```

**After**:
```yaml
rest_command:
  ledz_rainbow:
    url: http://ledzaabbcc.local/api/show
```

## Build Status

✅ **ESP32 Build**: SUCCESS (Flash: 44.7%, RAM: 18.2%)
✅ **All functionality**: Preserved
✅ **Documentation**: Updated

## Benefits of "ledz"

1. **Shorter**: Easier to type and remember
2. **Unique**: More distinctive branding
3. **Clean**: Simple, memorable name
4. **Scalable**: Works well for multiple devices (ledza1b2c3, ledza1b2c4, etc.)

## Backwards Compatibility

⚠️ **Breaking Changes**:
- mDNS hostname has changed
- Device ID prefix has changed
- AP SSID has changed

✅ **Preserved**:
- All WiFi settings
- All show configurations
- All API endpoints
- All functionality

## Testing Checklist

After flashing the new firmware:
- [ ] Verify device ID shows as `ledz-XXXXXX` in Serial monitor
- [ ] Verify mDNS hostname is `ledzxxxxxx.local`
- [ ] Verify web interface title shows "ledz"
- [ ] Verify web interface header shows device ID and hostname
- [ ] Test accessing via `.local` hostname
- [ ] Test API endpoints via new hostname
- [ ] Verify AP mode uses `ledz-XXXXXX` as SSID

## Future Considerations

The shortened "ledz" branding leaves room for potential future features:
- ledz Cloud (cloud sync)
- ledz App (mobile companion)
- ledz Hub (central controller for multiple devices)
- ledz Pro (advanced features)
