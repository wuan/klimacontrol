## ADDED Requirements

### Requirement: MQTT configuration model

The firmware SHALL persist MQTT configuration in NVS as `MqttConfig` with fields `host`, `port`, `username`, `password`, `prefix`, `interval`, and `enabled`. Default values SHALL be: `port = 1883`, `prefix = "sensors"`, `interval = 15` seconds, `enabled = false`.

#### Scenario: Default config on fresh device

- **WHEN** the firmware boots with no stored MQTT config
- **THEN** `loadMqttConfig()` SHALL return the defaults above and MQTT SHALL remain disabled

### Requirement: PubSubClient as the underlying library

The firmware SHALL use the `PubSubClient` library for MQTT. The client ID SHALL be set to the device's `device_id`. The firmware SHALL only publish at QoS 0 — `PubSubClient::publish()` does not support higher QoS levels, so no QoS parameter SHALL be exposed to callers.

#### Scenario: Client ID

- **WHEN** `MqttClient::connect()` is called
- **THEN** the client identifier passed to the broker SHALL equal the device's `device_id`

### Requirement: Server target handling

When the configured host string is an IP address, the firmware SHALL pass it to PubSubClient via the `setServer(IPAddress, port)` overload (which stores the IP by value). When the host is a DNS name, the firmware SHALL store the hostname string in a long-lived buffer (the `MqttConfig` member) and pass that pointer to `setServer(const char*, port)` — passing a function parameter would create a dangling pointer because `setServer` does not copy the string.

#### Scenario: Numeric host

- **WHEN** the configured host parses as a valid IPv4 address
- **THEN** PubSubClient SHALL receive the address via the IP overload, avoiding a DNS lookup on every connect attempt

#### Scenario: Hostname stored long-lived

- **WHEN** the configured host is a DNS name
- **THEN** the pointer passed to `setServer(const char*, …)` SHALL refer to the `MqttConfig` member buffer, not to a local parameter

### Requirement: Connection management

The firmware SHALL attempt to (re)connect to the broker when enabled and disconnected. Each connect attempt SHALL first call `wifiClient.stop()` to close any stale underlying socket, because PubSubClient does not reliably clean up failed sockets between attempts. Reconnect retries SHALL run in the network task's 1-second loop, not in the LED's 50 ms path — `connect()` can block up to ~2 seconds and SHALL NOT be allowed to stall responsive UI updates.

#### Scenario: Stale socket cleanup

- **WHEN** a previous `mqttClient.connect()` attempt failed
- **THEN** the next attempt SHALL call `wifiClient.stop()` before invoking `connect()`

#### Scenario: Reconnect cadence

- **WHEN** MQTT is enabled and the client is disconnected
- **THEN** reconnect attempts SHALL be made from the 1-second loop, with at most one connect attempt per loop iteration

### Requirement: Publish topic format and payload

Sensor measurements SHALL be published to topics of the form `{prefix}/{measurement_type}`. The payload SHALL be a JSON object containing `time` (epoch seconds, or 0 if NTP has not synced), `value` (numeric), `unit` (string), `sensor` (string), and `calculated` (boolean). Publishing SHALL only begin once the device has been up for at least 60 seconds, to avoid burst publishes during early boot.

#### Scenario: Publish path

- **WHEN** the configured prefix is `home/sensors` and a temperature measurement is available
- **THEN** the measurement SHALL be published to `home/sensors/temperature` with a JSON payload that includes the `time`, `value`, `unit`, `sensor`, and `calculated` fields

#### Scenario: 60-second startup hold

- **WHEN** the device has been up for less than 60 seconds since boot
- **THEN** no MQTT publishes SHALL be issued, even if the publish interval has elapsed

### Requirement: Last Will and Testament

The firmware SHALL register a Last Will and Testament (LWT) when connecting. The LWT topic SHALL be `{prefix}/status` and the LWT message SHALL be `offline`. On clean disconnect the firmware SHALL publish `online` to the same topic before disconnecting.

#### Scenario: Broker publishes LWT on crash

- **WHEN** the firmware loses its broker connection without sending an `MQTT DISCONNECT` (e.g., a crash or power loss)
- **THEN** the broker SHALL publish `offline` to `{prefix}/status` to notify subscribers
