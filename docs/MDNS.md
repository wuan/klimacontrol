# mDNS (Multicast DNS) Support

## Overview

ledz automatically advertises itself on the local network using mDNS, allowing you to access it via a friendly hostname instead of having to remember the IP address.

## How It Works

When the device connects to WiFi (either in AP or STA mode), it automatically:
1. Generates a hostname based on the device ID (last 3 bytes of MAC address)
2. Starts an mDNS responder
3. Advertises HTTP service on port 80

## Hostname Format

The hostname follows this pattern:
- **Device ID**: `ledz-AABBCC` (where AABBCC are the last 3 bytes of MAC address)
- **mDNS Hostname**: `ledzaabbcc.local`

Example:
- If your device ID is `ledz-A1B2C3`
- Your mDNS hostname will be `ledza1b2c3.local`

## Accessing the Controller

### Via mDNS Hostname

You can access ledz at:
```
http://ledzaabbcc.local/
```

Replace `aabbcc` with your device's unique identifier.

### Via IP Address

Traditional IP access still works:
```
http://192.168.1.XXX/
```

## Where to Find Your Hostname

### 1. Serial Monitor Output

When the device connects to WiFi, the Serial monitor displays:
```
Connected!
IP address: 192.168.1.123
mDNS responder started: ledza1b2c3.local
You can now access ledz at:
  http://ledza1b2c3.local/
  or http://192.168.1.123
```

### 2. Web Interface Header

The web interface displays your device ID and mDNS hostname:
```
ledz-A1B2C3
Access at: ledza1b2c3.local
```

## Service Advertisement

ledz advertises the following mDNS service:
- **Service Type**: `_http._tcp`
- **Port**: 80
- **Name**: Device hostname (e.g., `ledza1b2c3`)

This allows mDNS-aware applications and network scanners to automatically discover ledz devices on the network.

## Compatibility

### Supported Platforms

✅ **macOS**: Native support, no additional software needed
✅ **iOS**: Native support, works in Safari and all browsers
✅ **Linux**: Works with Avahi daemon (usually pre-installed)
✅ **Windows**: Requires Bonjour service (comes with iTunes or can be installed separately)
✅ **Android**: Supported in most modern browsers

### Troubleshooting

**Problem**: Cannot access via `.local` hostname

**Solutions**:
1. **Windows**: Install Bonjour Print Services from Apple
2. **Linux**: Ensure Avahi is running: `sudo systemctl start avahi-daemon`
3. **Network**: Make sure you're on the same network as ledz
4. **Firewall**: Check that mDNS traffic (UDP port 5353) isn't blocked
5. **Fallback**: Use the IP address directly

**Problem**: mDNS not starting

Check the Serial monitor for error messages:
```
Error starting mDNS responder!
```

This usually indicates a WiFi connection issue or memory constraint.

## Benefits

### User-Friendly Access
- No need to remember IP addresses
- Works even if DHCP assigns a different IP
- Consistent hostname across device restarts

### Network Discovery
- Automatic discovery by compatible applications
- Works across multiple ledz devices (each has unique hostname)
- Integrates with home automation systems

### Mobile-Friendly
- Easy access from phones/tablets
- No manual IP address entry
- Works with QR code generators

## Examples

### Quick Access
```bash
# From any device on the network
open http://ledza1b2c3.local/

# Using curl
curl http://ledza1b2c3.local/api/status

# Set show via API
curl -X POST http://ledza1b2c3.local/api/show \
  -H "Content-Type: application/json" \
  -d '{"name":"Rainbow"}'
```

### Home Automation Integration

**Home Assistant** (configuration.yaml):
```yaml
rest_command:
  ledz_rainbow:
    url: http://ledza1b2c3.local/api/show
    method: POST
    payload: '{"name":"Rainbow"}'
    content_type: 'application/json'
```

### Network Scanning

Find all ledz devices on your network:
```bash
# macOS/Linux
dns-sd -B _http._tcp

# Or use Avahi
avahi-browse -r _http._tcp
```

## Implementation Details

### Code Location
- **Header**: `src/Network.h` - ESPmDNS include
- **Implementation**: `src/Network.cpp` - `startAP()` and `startSTA()` methods

### Memory Usage
- ESPmDNS library adds ~2KB to flash
- Minimal RAM overhead (~200 bytes)
- No performance impact on LED rendering

### Initialization Sequence
1. WiFi connection established
2. IP address obtained (DHCP or static)
3. mDNS responder started with device hostname
4. HTTP service advertised on port 80

## Advanced Usage

### Custom Hostname

If you want to change the hostname format, modify `src/Network.cpp`:

```cpp
// Current implementation
String hostname = deviceId;
hostname.toLowerCase();
hostname.replace("ledz-", "ledz");

// Custom implementation (example)
hostname = "myled";  // All devices would be "myled.local"
```

### Multiple Services

You can advertise additional services:

```cpp
// In Network.cpp after MDNS.begin()
MDNS.addService("http", "tcp", 80);
MDNS.addService("led-control", "tcp", 80);  // Custom service
```

## Security Considerations

### Open Access
mDNS makes your device discoverable on the local network. This is generally safe for home networks but consider:

1. **Network Segmentation**: Place ledz devices on a separate VLAN if security is critical
2. **Firewall Rules**: Block mDNS (port 5353) at network boundaries
3. **Authentication**: Consider adding HTTP Basic Auth (future enhancement)

### Privacy
The hostname includes the MAC address suffix, which is unique to your device. This is standard practice and doesn't present significant privacy concerns on trusted networks.

## Future Enhancements

Potential improvements for mDNS support:
- [ ] Custom hostname configuration via web UI
- [ ] TXT record with firmware version, model, capabilities
- [ ] Automatic discovery by companion mobile app
- [ ] Integration with HomeKit (requires additional protocols)
- [ ] OTA update discovery and coordination

## References

- [RFC 6762](https://tools.ietf.org/html/rfc6762) - Multicast DNS specification
- [ESPmDNS Library](https://github.com/espressif/arduino-esp32/tree/master/libraries/ESPmDNS) - ESP32 implementation
- [DNS-SD](https://www.dns-sd.org/) - DNS Service Discovery
