#ifndef KLIMACONTROL_NET_MDNS_ADVERTISER_H
#define KLIMACONTROL_NET_MDNS_ADVERTISER_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <string>
using String = std::string;
#endif

namespace Config {
    class ConfigManager;
}

namespace Net {

    /**
     * Device hostname and mDNS advertisement. The hostname is `klima-aabbcc`
     * (lowercase device ID; the ID itself is `klima-AABBCC`), so the device is
     * reachable as `klima-aabbcc.local`. The mDNS instance name is the
     * user-facing "Klima <device name>" (or "Klima <device id>" when no name is
     * configured).
     */
    class MdnsAdvertiser {
    public:
        explicit MdnsAdvertiser(Config::ConfigManager &config) : config(config) {}

        /** Cached; computed on first use. */
        const String &hostname();

        /**
         * (Re)start the mDNS responder and advertise the HTTP service. Safe to
         * call again after a reconnect; MDNS.begin() restarts the responder.
         */
        void advertise();

    private:
        Config::ConfigManager &config;
        String cachedHostname;
        String instanceName; // must outlive MDNS.setInstanceName()
    };

} // namespace Net

#endif // KLIMACONTROL_NET_MDNS_ADVERTISER_H
