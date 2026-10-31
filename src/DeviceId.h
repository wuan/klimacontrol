// Device ID generation helper
// Generates unique device identifier from ESP32 MAC address
//

#ifndef KLIMACONTROL_DEVICEID_H
#define KLIMACONTROL_DEVICEID_H

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_system.h>

namespace DeviceId {
    /**
     * Get unique device ID based on MAC address
     * Format: "AABBCC" where AABBCC are the last 3 bytes of MAC
     * @return Pointer to a cached static buffer — valid for the device's
     *         lifetime, not a String allocated per call. The eFuse MAC never
     *         changes at runtime, so the ID is formatted once on first call.
     *         The first call happens in setup() before any task exists.
     */
    inline const char* getDeviceId() {
        static char id[7] = "";
        if (id[0] == '\0') {
            uint64_t mac = ESP.getEfuseMac();

            // Extract last 3 bytes from MAC (bytes 3, 4, 5 of uint64_t)
            uint8_t b1 = (mac >> 24) & 0xFF;
            uint8_t b2 = (mac >> 32) & 0xFF;
            uint8_t b3 = (mac >> 40) & 0xFF;

            snprintf(id, sizeof(id), "%02X%02X%02X", b1, b2, b3);
        }
        return id;
    }

    /**
     * Get full MAC address as string
     * Format: "AA:BB:CC:DD:EE:FF"
     * @return Pointer to a cached static buffer, as above
     */
    inline const char* getMacAddress() {
        static char macStr[18] = "";
        if (macStr[0] == '\0') {
            uint64_t mac = ESP.getEfuseMac();
            uint8_t mac_bytes[6];
            memcpy(mac_bytes, &mac, 6);

            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", mac_bytes[0], mac_bytes[1], mac_bytes[2],
                     mac_bytes[3], mac_bytes[4], mac_bytes[5]);
        }
        return macStr;
    }
} // namespace DeviceId

#else
#include <string>
using String = std::string;

namespace DeviceId {
    inline const char* getDeviceId() {
        return "000000";
    }
    inline const char* getMacAddress() {
        return "00:00:00:00:00:00";
    }
} // namespace DeviceId

#endif // ARDUINO

#endif // KLIMACONTROL_DEVICEID_H
