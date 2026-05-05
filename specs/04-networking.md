# Networking Specification

## Purpose

Define WiFi connectivity, network modes (STA and AP), mDNS, captive portal, NTP.

## Requirements

### Network Modes
1. **S4.1** SHALL support STA mode for normal operation
2. **S4.2** SHALL support AP mode for initial setup
3. **S4.3** SHALL enter AP mode on first boot (unconfigured)
4. **S4.4** SHALL enter AP mode after 3 consecutive connection failures

### AP Mode
5. **S4.5** SHALL use SSID format: `klima-AABBCC` (last 3 MAC bytes)
6. **S4.6** SHALL use IP: 192.168.4.1
7. **S4.7** SHALL implement captive portal with DNS redirection
8. **S4.8** SHALL serve configuration page at /config

### STA Mode
9. **S4.9** SHALL connect to configured WiFi network
10. **S4.10** SHALL auto-reconnect if connection lost
11. **S4.11** SHALL use DHCP to obtain IP address

### mDNS
12. **S4.12** SHALL advertise via mDNS as klima-aabbcc.local
13. **S4.13** SHALL advertise HTTP service on port 80

### NTP
14. **S4.14** SHALL synchronize time via NTP every 300 seconds
15. **S4.15** SHALL use UTC timezone by default

### Connection Failure
16. **S4.16** SHALL track consecutive connection failures
17. **S4.17** SHALL reset counter on successful connection
18. **S4.18** SHALL enter AP mode when failures reach 3

## Scenarios

### Scenario S4-A: First Boot AP Mode
Given device boots for first time,
When Network::begin() is called,
Then device SHALL enter AP mode with SSID klima-AABBCC, IP 192.168.4.1

### Scenario S4-B: WiFi Configuration
Given device in AP mode,
When user submits SSID and password,
Then credentials SHALL be saved, device SHALL restart into STA mode

### Scenario S4-C: STA Connection
Given WiFi is configured,
When Network::begin() is called,
Then device SHALL connect to WiFi, start webserver, enable mDNS and NTP

### Scenario S4-D: Connection Failure Fallback
Given 3 consecutive connection failures,
Then device SHALL enter AP mode with BLINK_SLOW LED

### Scenario S4-E: mDNS Discovery
Given device in STA mode with IP 192.168.1.100,
When user queries klima-aabbcc.local,
Then DNS SHALL resolve to 192.168.1.100
