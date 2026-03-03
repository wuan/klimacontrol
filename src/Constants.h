#ifndef KLIMACONTROL_CONSTANTS_H
#define KLIMACONTROL_CONSTANTS_H

/**
 * Project-wide constants
 */
namespace Constants {
    // Project name
    constexpr const char* PROJECT_NAME = "klima";
    
    // mDNS/hostname prefix
    constexpr const char* HOSTNAME_PREFIX = "klima-";
    
    // AP mode SSID prefix
    constexpr const char* AP_SSID_PREFIX = "Klima ";
    
    // mDNS instance name prefix
    constexpr const char* INSTANCE_NAME_PREFIX = "Klima ";
    
    // NVS namespace
    constexpr const char* NVS_NAMESPACE = "klima";
    
    // GitHub repository name
    constexpr const char* GITHUB_REPO = "klima";
};

#endif //KLIMACONTROL_CONSTANTS_H