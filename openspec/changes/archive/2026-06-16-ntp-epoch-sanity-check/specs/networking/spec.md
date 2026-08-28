## MODIFIED Requirements

### Requirement: NTP time synchronization

In STA mode the firmware SHALL synchronize wall-clock time via NTP, using `NTPClient`. The initial sync SHALL be attempted immediately after association via `forceUpdate()`. Once synced, the firmware SHALL refresh the time every 1 hour. If the initial sync fails, the firmware SHALL retry once per minute until the first sync succeeds.

A sync SHALL only count as successful when **both** `forceUpdate()` returns `true` AND the resulting epoch lies in the plausibility range `[NTP_MIN_VALID_EPOCH, NTP_MAX_VALID_EPOCH]` (year 2020 to year 2100 UTC). When `forceUpdate()` returns `true` but the epoch is not plausible, the firmware SHALL treat the sync as failed: emit an `ESP_LOGE` line with the bogus value, increment `ntpBogusSyncCount`, and (depending on the call site) either keep `ntpSynced = false` and let the existing retry timer fire, or keep the previous `lastNtpUpdateEpoch` and `ntpSynced = true` so the device does not flap on a one-off bad refresh.

#### Scenario: First sync after association

- **WHEN** STA association succeeds AND `ntpClient.forceUpdate()` returns `true` AND the resulting `ntpClient.getEpochTime()` lies in the plausibility range
- **THEN** the firmware SHALL record the synced epoch and expose it via `getCurrentEpoch()`

#### Scenario: Bogus epoch is rejected at initial sync

- **WHEN** `ntpClient.forceUpdate()` returns `true` but the resulting epoch is outside the plausibility range (e.g. 0, a small value, or any value > `NTP_MAX_VALID_EPOCH`)
- **THEN** the firmware SHALL NOT set `ntpSynced = true`, SHALL leave `lastNtpUpdateEpoch` unchanged, SHALL emit an `ESP_LOGE` line that includes the bogus value, and SHALL increment `ntpBogusSyncCount`

#### Scenario: Bogus epoch is rejected on a periodic refresh

- **WHEN** the device is already synced and a periodic 1-hour `forceUpdate()` returns `true` but the resulting epoch is outside the plausibility range
- **THEN** the firmware SHALL keep the previous `lastNtpUpdateEpoch` and keep `ntpSynced = true`, SHALL emit an `ESP_LOGE` line that includes the bogus value, SHALL call `reportInternetFailure()`, and SHALL increment `ntpBogusSyncCount`

#### Scenario: Epoch before sync

- **WHEN** NTP has not yet synced
- **THEN** `getCurrentEpoch()` SHALL return 0, even though `NTPClient::getEpochTime()` would otherwise return elapsed-since-boot
