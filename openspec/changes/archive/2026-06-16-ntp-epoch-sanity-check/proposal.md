## Why

`Network::connectSTA()` and the periodic refresh in the Network task
both call `safeNtpUpdate()` and treat its `true` return value as
"synced" — `ntpSynced = true; lastNtpUpdateEpoch = ntpClient.getEpochTime();`
(`src/Network.cpp:278-280`, `src/Network.cpp:662-664`). The
`NTPClient::forceUpdate()` library method reports success based on
its own internal UDP-exchange logic, but does not validate that the
resulting `_currentEpoc` is in a plausible range. In every code
path that calls `getCurrentEpoch()` (the MQTT publish path at
`src/Network.cpp:771`, the JSON status response, anywhere else that
emits a timestamp), the value goes straight to the broker or the
user. A bogus epoch (zero, near-zero, a corrupted value from a
malformed NTP response) is silently published as if it were
correct. The mqtt-integration spec already documents `"time": 0`
as the "NTP not yet synced" sentinel
(`openspec/specs/mqtt-integration/spec.md:54`); the bug is that
"synced" can also produce a 0, so the sentinel becomes ambiguous.

## What Changes

- Add a `static constexpr uint32_t NTP_MIN_VALID_EPOCH = 1577836800;`
  (2020-01-01T00:00:00Z) and `static constexpr uint32_t NTP_MAX_VALID_EPOCH = 4102444800;`
  (2100-01-01T00:00:00Z) in `src/Network.h` as the sanity bounds
  for a "plausible" NTP epoch.
- Introduce a `static bool isNtpEpochPlausible(uint32_t epoch)`
  helper (header-inline, in `src/Network.h`) that returns
  `epoch >= NTP_MIN_VALID_EPOCH && epoch <= NTP_MAX_VALID_EPOCH`.
- Change the two NTP success sites in `src/Network.cpp` (the
  initial sync in `connectSTA()` and the unsynced-retry in
  `Network::loop()`) to **only** set `ntpSynced = true` and
  update `lastNtpUpdateEpoch` when the epoch returned by
  `ntpClient.getEpochTime()` passes the plausibility check.
  When `safeNtpUpdate()` returns `true` but the epoch is not
  plausible, the call SHALL be treated as a failed sync: log
  an `ESP_LOGE` line identifying the bogus epoch value and
  leave `ntpSynced` unchanged.
- Apply the same check inside the periodic 1-hour refresh
  branch (`src/Network.cpp:642-658`): if `safeNtpUpdate()`
  returns `true` but the new epoch is not plausible, keep the
  previous `lastNtpUpdateEpoch` and keep `ntpSynced = true`
  (the old value is still usable, just stale); emit an
  `ESP_LOGE` and call `reportInternetFailure()`. The device
  is not put back into the unsynced state for a one-off
  bogus refresh — that would re-trigger the 1-minute retry
  loop unnecessarily.
- Add a `uint32_t ntpBogusSyncCount` counter on `Network`
  (private; not exposed via the API in this change) that
  increments every time a sync passes the boolean check
  but fails the epoch plausibility check, regardless of
  which site it happened at. This is a developer-facing
  signal for the field — useful in `ESP_LOGE` output to
  spot a misbehaving NTP server or a corrupted UDP
  response.
- Update the `networking` spec's "NTP time synchronization"
  requirement to assert the plausibility check explicitly:
  `safeNtpUpdate()` returning `true` is necessary but not
  sufficient; the epoch MUST also lie in the plausibility
  range for the sync to count.

No runtime semantics change for the happy path (a real NTP
response from any well-behaved server in 2020+ already lands
inside the bounds). The change only affects the failure
path: bogus epochs are now rejected instead of silently
propagated.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `networking`: the "NTP time synchronization" requirement gets
  a new scenario that asserts "sync only counts when the
  returned epoch is plausible", and the existing "First sync
  after association" scenario is widened to make the
  plausibility check explicit in the WHEN clause. No
  behavioural change to the happy path; the new scenario
  covers the new failure path.

## Impact

- **Source files (refactor)**:
  - `src/Network.h` — add `NTP_MIN_VALID_EPOCH`,
    `NTP_MAX_VALID_EPOCH` constants and the
    `isNtpEpochPlausible()` inline helper. Add
    `ntpBogusSyncCount` counter field.
  - `src/Network.cpp` — wrap the two `ntpSynced = true`
    sites (lines 278-280 in `connectSTA()` and 662-664 in
    `Network::loop()`'s unsynced-retry branch) with the
    plausibility check. Add the same check in the periodic
    refresh branch (642-658). Increment
    `ntpBogusSyncCount` whenever a sync passes the boolean
    check but fails the epoch check.
  - `test/test_network_ntp_watchdog/` (or a new
    `test_ntp_epoch_sanity/` directory) — add unit tests
    for `isNtpEpochPlausible()` covering the boundaries
    (0, 1577836800 - 1, 1577836800, 1577836800 + 1,
    4102444800, 4102444800 + 1, UINT32_MAX).
- **Spec files (text update)**:
  - `openspec/specs/networking/spec.md` — the "NTP time
    synchronization" requirement gets a new scenario for
    the bogus-epoch failure case, and the existing
    "First sync after association" scenario is tightened
    to require the epoch plausibility check.
- **No dependency changes** — uses only the existing
  Arduino `NTPClient` library.
- **No API/JSON contract changes** — `ntpBogusSyncCount`
  is internal in this change; surfacing it via
  `/api/status` is a follow-up if telemetry data shows it
  is useful in the field.
- **Out of scope**: changing the plausibility bounds
  (2020-2100 is generous enough for any real deployment),
  adding an NTP server health query, switching to NTS,
  exposing `ntpBogusSyncCount` via the web UI.
