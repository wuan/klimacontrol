// Device ID generation helper
// Generates unique device identifier from ESP32 MAC address
//

#ifndef LEDZ_DEVICEID_H
#define LEDZ_DEVICEID_H

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_system.h>

namespace DeviceId {
    /**
     * Get unique device ID based on MAC address
     * Format: "AABBCC" where AABBCC are the last 3 bytes of MAC
     * @return Device ID string
     */
    inline String getDeviceId() {
        uint64_t mac = ESP.getEfuseMac();
        uint8_t mac_bytes[6];
        memcpy(mac_bytes, &mac, 6);

        char id[16];
        snprintf(id, sizeof(id), "%02X%02X%02X",
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
        return String(id);
    }

    /**
     * Get full MAC address as string
     * Format: "AA:BB:CC:DD:EE:FF"
     * @return MAC address string
     */
    inline String getMacAddress() {
        uint64_t mac = ESP.getEfuseMac();
        uint8_t mac_bytes[6];
        memcpy(mac_bytes, &mac, 6);

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_bytes[0], mac_bytes[1], mac_bytes[2],
                 mac_bytes[3], mac_bytes[4], mac_bytes[5]);
        return String(mac_str);
    }
} // namespace DeviceId

#endif // ARDUINO

#endif //LEDZ_DEVICEID_H