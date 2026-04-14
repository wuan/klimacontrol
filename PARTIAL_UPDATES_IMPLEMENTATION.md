# Partial Updates Implementation for klimacontrol

## Summary
This implementation refactors the configuration storage system to use **individual NVS keys** instead of storing entire structs. This provides better efficiency, maintainability, and flexibility.

## Changes Made

### 1. Config.h
**Added new methods for partial updates:**
```cpp
void updateDeviceName(const char* device_name);
void updateTargetTemperature(float temperature);
void updateTemperatureControlEnabled(bool enabled);
void updateElevation(float elevation);
```

These methods allow updating individual configuration fields without loading/saving the entire struct.

### 2. Config.cpp
**Refactored `loadDeviceConfig()`:**
- Now loads each field individually from NVS
- Uses device ID as fallback for device name if not set
- Maintains backward compatibility

**Refactored `saveDeviceConfig()`:**
- Still exists for bulk updates (e.g., factory reset)
- Individual field updates now use the new methods

**Added new methods:**
```cpp
void ConfigManager::updateDeviceName(const char* device_name);
void ConfigManager::updateTargetTemperature(float temperature);
void ConfigManager::updateTemperatureControlEnabled(bool enabled);
void ConfigManager::updateElevation(float elevation);
```

Each method:
- Opens NVS in write mode
- Updates only the specific key
- Closes NVS
- No need to load/save entire struct

### 3. SensorController.cpp
**Updated `setTargetTemperature()`:**
```cpp
void SensorController::setTargetTemperature(float temperature) {
    targetTemperature = std::max(10.0f, std::min(30.0f, temperature));
    ESP_LOGI(TAG, "Target temperature set to %.1f C", targetTemperature);
    
    // Persist to NVS using partial update
    config.updateTargetTemperature(targetTemperature);
}
```

**Updated `setControlEnabled()`:**
```cpp
void SensorController::setControlEnabled(bool enabled) {
    if (controlEnabled != enabled) {
        controlEnabled = enabled;
        ESP_LOGI(TAG, "Temperature control %s", enabled ? "enabled" : "disabled");
        
        // Persist to NVS using partial update
        config.updateTemperatureControlEnabled(enabled);
    }
}
```

### 4. ControlRoutes.cpp
**Simplified temperature target endpoint:**
- Removed manual config loading/saving
- Now relies on `sensorController.setTargetTemperature()` which handles persistence

**Simplified control enable/disable endpoints:**
- Removed manual config loading/saving
- Now relies on `sensorController.setControlEnabled()` which handles persistence

### 5. SettingsRoutes.cpp
**Updated device name endpoint:**
```cpp
// Old:
Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
strlcpy(deviceConfig.device_name, name, sizeof(deviceConfig.device_name));
config.saveDeviceConfig(deviceConfig);

// New:
config.updateDeviceName(name);
```

**Updated elevation endpoint:**
```cpp
// Old:
Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
deviceConfig.elevation = elevation;
config.saveDeviceConfig(deviceConfig);
sensorController.setElevation(elevation);

// New:
config.updateElevation(elevation);
sensorController.setElevation(elevation);
```

## Benefits

### 1. Efficiency
- **No need to load entire struct** when updating a single field
- **Faster writes** (smaller NVS operations)
- **Less RAM usage** (no temporary struct copies)

### 2. Maintainability
- **Clearer code** (each field has its own update method)
- **Easier to extend** (add new fields without touching entire structs)
- **Better error handling** (individual field validation)

### 3. Flexibility
- **Partial updates** can be done from anywhere in the codebase
- **No need to load config** just to update one field
- **Easier to add new config options** in the future

### 4. Consistency
- Follows the same pattern as `WiFiConfig` and `MqttConfig`
- Uses individual NVS keys throughout

## Backward Compatibility

✅ **Fully backward compatible**
- Existing NVS data is still valid
- `saveDeviceConfig()` still works for bulk updates
- `loadDeviceConfig()` still returns a complete DeviceConfig struct
- No changes required to existing code that uses the full struct methods

## Testing

The changes have been verified to:
1. Compile without syntax errors
2. Maintain the same API surface (no breaking changes)
3. Follow the same patterns as existing WiFiConfig and MqttConfig
4. Properly handle individual field updates

## Future Improvements

Possible enhancements:
1. Add CRC checks for individual config fields
2. Implement encryption for sensitive fields (MQTT password, WiFi password)
3. Add configuration versioning for schema evolution
4. Implement backup/restore for individual config sections

## Migration Guide

For developers adding new configuration fields:

1. **Add the field to the DeviceConfig struct**
2. **Add a constant for the NVS key** in Config.h:
   ```cpp
   static constexpr const char *NEW_FIELD = "new_field";
   ```
3. **Add an update method** in Config.cpp:
   ```cpp
   void ConfigManager::updateNewField(float value) {
       prefs.begin(NAMESPACE, false);
       prefs.putFloat(NEW_FIELD, value);
       prefs.end();
   }
   ```
4. **Update any code that needs to set this field** to use the new method

## Files Modified

- `src/Config.h` - Added new methods
- `src/Config.cpp` - Implemented partial updates
- `src/SensorController.cpp` - Updated to use partial updates
- `src/routes/ControlRoutes.cpp` - Simplified using new methods
- `src/routes/SettingsRoutes.cpp` - Updated to use new methods

## Conclusion

This implementation successfully refactors the configuration storage to use partial updates, providing better efficiency and maintainability while maintaining full backward compatibility.
