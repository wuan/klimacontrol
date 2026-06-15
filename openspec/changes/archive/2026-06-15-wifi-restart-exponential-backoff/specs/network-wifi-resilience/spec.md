## ADDED Requirements

### Requirement: Exponential backoff on boot-time STA failure

After `startSTA()` returns with `WiFi.status() != WL_CONNECTED` and the persisted `wifi_failures` counter has been incremented, the Network task SHALL wait for a delay computed by `Network::staFailureBackoffMs(failures)` milliseconds before calling `ESP.restart()`. The delay SHALL follow a doubling schedule starting at 2000 ms with a 300000 ms (5 minute) cap: `delay_ms = min(2000 * 2^(failures-1), 300000)`. Concretely: 2 s, 4 s, 8 s, 16 s, 32 s, 64 s, 128 s, 256 s, 300 s (capped), 300 s, …. The counter is reset to 0 on the next successful association, so the curve measures *consecutive* failures.

The first failure SHALL wait 2000 ms (preserving the pre-change behavior). Each subsequent failure SHALL wait at least twice as long as the previous failure, until the cap is reached. The cap SHALL hold for every failure after the one that first reaches it (i.e. no failure waits longer than 300000 ms). The chosen delay SHALL be logged alongside the failure count so the schedule is observable in the serial log.

The existing AP-fallback threshold (`failures % 3 == 0`, opening AP mode for 5 minutes) is unchanged by this requirement.

#### Scenario: First failure uses the 2 s base

- **WHEN** `Network::task()` increments `wifi_failures` to 1 and the new delay is computed
- **THEN** `staFailureBackoffMs(1)` returns 2000 and the network task waits 2000 ms before calling `ESP.restart()`

#### Scenario: Delay doubles until the cap

- **WHEN** the failure count is 1, 2, 3, 4, 5, 6, 7, 8 and the new delay is computed
- **THEN** `staFailureBackoffMs` returns 2000, 4000, 8000, 16000, 32000, 64000, 128000, 256000 respectively (in ms)

#### Scenario: Delay saturates at the 5 minute cap

- **WHEN** the failure count is 9, 10, 12, 32, 200 and the new delay is computed
- **THEN** `staFailureBackoffMs` returns 300000 for every value at or above 9, never exceeding the cap

#### Scenario: Successful reset clears the curve

- **WHEN** the device successfully associates with the AP after a streak of failed boots
- **THEN** the next failure uses the 2000 ms base (the counter has been reset to 0)
