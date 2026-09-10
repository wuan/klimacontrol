#include "network/MdnsAdvertiser.h"

#include <cstring>

#include "Config.h"
#include "Constants.h"
#include "Log.h"

#ifdef ARDUINO
#include <ESPmDNS.h>
#include "DeviceId.h"
#endif

static constexpr const char *const TAG = "net";

namespace Net {

    const String &MdnsAdvertiser::hostname() {
#ifdef ARDUINO
        if (cachedHostname.isEmpty()) {
            cachedHostname = Constants::HOSTNAME_PREFIX + DeviceId::getDeviceId();
            cachedHostname.toLowerCase();
        }
#else
        if (cachedHostname.empty()) cachedHostname = Constants::PROJECT_NAME;
#endif
        return cachedHostname;
    }

    void MdnsAdvertiser::advertise() {
#ifdef ARDUINO
        const String &host = hostname();

        if (!MDNS.begin(host.c_str())) {
            ESP_LOGE(TAG, "Error starting mDNS responder");
            return;
        }
        ESP_LOGI(TAG, "mDNS responder started: %s.local", host.c_str());

        const Config::DeviceConfig deviceConfig = config.loadDeviceConfig();
        const bool hasCustomName = deviceConfig.device_name[0] != '\0'
                                   && strcmp(deviceConfig.device_name, deviceConfig.device_id) != 0;
        instanceName = Constants::INSTANCE_NAME_PREFIX
                       + String(hasCustomName ? deviceConfig.device_name : deviceConfig.device_id);

        ESP_LOGI(TAG, "mDNS instance name: '%s'", instanceName.c_str());
        MDNS.setInstanceName(instanceName.c_str());
        MDNS.addService("http", "tcp", 80);
#endif
    }

} // namespace Net
