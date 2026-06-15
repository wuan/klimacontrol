## ADDED Requirements

### Requirement: Network task blocking-call safety

Every external (non-loopback) network call made from the Network task SHALL either bound its own execution time (via a documented socket, library, or application-level timeout) or feed the FreeRTOS task watchdog (`esp_task_wdt_reset()`) immediately before and after the call. This contract exists so that a hung UDP socket, a stalled TCP connection, or any other blocking call inside the task cannot starve the 30 s task watchdog and force a panic reset that may land inside an NVS write.

The NTP update is the canonical example: `Network::safeNtpUpdate()` SHALL call `esp_task_wdt_reset()` before and after `ntpClient.forceUpdate()` and SHALL return the NTPClient result unchanged. Any future external network call added to the Network task MUST follow the same pattern (helper or inline WDT feeds) and MUST be added to the audit table in the change design.

#### Scenario: NTP call completes normally

- **WHEN** the Network task calls `safeNtpUpdate()` and `ntpClient.forceUpdate()` returns `true` within the library timeout
- **THEN** `safeNtpUpdate()` returns `true` and the watchdog is fed at least twice (before and after the NTP exchange)

#### Scenario: NTP call times out

- **WHEN** the Network task calls `safeNtpUpdate()` and the underlying UDP socket is unreachable so `ntpClient.forceUpdate()` returns `false` after its internal timeout
- **THEN** `safeNtpUpdate()` returns `false` and the watchdog is still fed (before the call and after the call returns)

#### Scenario: NTP call would exceed TWDT budget on a degraded link

- **WHEN** the underlying `WiFiUDP::parsePacket()` blocks for longer than the per-iteration watchdog budget due to lwIP retransmits
- **THEN** the watchdog is fed before the call (resetting the 30 s budget) and again after the call returns, so the panic handler does not fire on a transiently hung link
