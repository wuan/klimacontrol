// Simple syntax check for the config changes
#include "src/Config.h"
#include "src/SensorController.h"

int main() {
    // Test that the new methods exist and have correct signatures
    Config::ConfigManager config;
    
    // These should compile without errors
    config.updateDeviceName("test");
    config.updateTargetTemperature(22.5f);
    config.updateTemperatureControlEnabled(true);
    config.updateElevation(100.0f);
    
    return 0;
}
