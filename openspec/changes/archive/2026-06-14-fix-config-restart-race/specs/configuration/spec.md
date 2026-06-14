## MODIFIED Requirements

### Requirement: Restart management

`ConfigManager` SHALL provide deferred restart scheduling so callers can complete an HTTP response before the device reboots. The API SHALL be `requestRestart(uint32_t delayMs)`, `isRestartPending()`, and `checkRestart()` (called from a main loop).

The deferred-restart state (the "requested" flag and the deadline timestamp) SHALL be stored in a single 64-bit word (low bit = flag, upper 63 bits = unsigned deadline) and SHALL be published and consumed as one indivisible pair — a reader in the main loop SHALL observe either the pre-request state ("not requested") or the post-request state ("requested with this exact deadline"), never a torn combination of the two. The producer and consumer SHALL serialize their access via a lightweight spinlock (e.g. `std::atomic_flag`) or a lock-free equivalent. The deadline comparison in `checkRestart()` SHALL continue to use the existing wrap-safe `int32_t` math (`static_cast<int32_t>(millis() - deadline) >= 0`) so behavior is unchanged ~49 days after boot.

#### Scenario: Deferred restart after settings change

- **WHEN** an HTTP handler calls `config.requestRestart(1000)` and returns its response
- **THEN** the device SHALL restart approximately 1 second later, after the response has been flushed to the client

#### Scenario: Atomic state handoff

- **WHEN** one task calls `config.requestRestart(500)` while another task concurrently calls `config.isRestartPending()`
- **THEN** `isRestartPending()` SHALL return either `false` (no request yet) or `true` (request observed with the matching 500 ms deadline), and SHALL never observe a "requested" flag paired with a stale or zeroed deadline
