## Context

The e-paper display's footer timestamp renders UTC. Tracing it back:
`DisplayManager::formatClock()` calls `Network::getCurrentEpoch()`
(`src/Network.h:218`), which returns `ntpClient.getEpochTime()`. `NTPClient` is
constructed as `ntpClient(wifiUdp)` (`src/Network.cpp:64`) — the library's
optional `timeOffset` argument is omitted, so it defaults to 0 and the epoch is
UTC. `formatClock()` then does `epoch % 86400` arithmetic, which cannot express
any offset at all.

Nothing else in the firmware formats a human-facing time: a grep for
`localtime|gmtime|setenv|tzset|configTime|strftime` across `src/` returns
nothing. Syslog emits no timestamp; MQTT publishes the raw epoch, which is
correct and must not change.

So this is a genuinely new capability with, today, exactly one consumer.

## Goals / Non-Goals

**Goals:**

- Render local time correctly, including daylight-saving transitions, without
  the user having to re-configure anything twice a year.
- Keep the mechanism host-testable, matching the project's `#ifdef ARDUINO`
  discipline.
- Cost no flash for zone data.
- Default to today's behaviour (UTC) so existing devices are unaffected.

**Non-Goals:**

- Shipping an IANA tzdata database. It is megabytes; the POSIX rule string is
  ~30 bytes and expresses the same transitions for a single zone.
- Network timezone lookup or geolocation. It adds a runtime dependency and a
  failure mode to something the user configures once.
- Timestamping syslog, or changing the MQTT epoch. Machine consumers want UTC.
- A 12-hour clock. The display footer has room for `HH:MM` and no more.

## Decisions

### D1. Store a POSIX TZ string, not an offset-plus-DST-flag

```
CET-1CEST,M3.5.0,M10.5.0/3
└┬┘└┬┘└─┬┘ └──┬──┘└───┬───┘
 │  │   │     │       └── DST ends:   last Sunday of October, 03:00
 │  │   │     └────────── DST starts: last Sunday of March, 02:00 (default)
 │  │   └──────────────── DST abbreviation
 │  └──────────────────── offset west of UTC, i.e. UTC+1
 └─────────────────────── standard abbreviation
```

**Rationale.** The transition rules are the hard part, and POSIX already encodes
them in a form newlib parses natively. ESP-IDF documents exactly this approach
for system time. The alternative — storing `utc_offset_minutes` plus a
`dst_enabled` bool — requires the firmware to own hardcoded transition rules,
which differ per region (EU switches on the last Sunday of March; the US on the
second Sunday of March; Australia inverts the seasons entirely) and change by
legislation. That is a table this project would have to maintain and ship
updates for.

Verified on the host before committing to it:

```
$ TZ='CET-1CEST,M3.5.0,M10.5.0/3'
2026-01-15 12:00Z (winter)   -> 13:00  isdst=0  CET
2026-07-15 12:00Z (summer)   -> 14:00  isdst=1  CEST
2026-03-29 00:59Z            -> 01:00  isdst=0  CET
2026-03-29 01:00Z            -> 03:00  isdst=1  CEST
```

The transition is exact: 01:00 CET is followed immediately by 03:00 CEST.

**Alternatives considered**

- *Fixed integer offset.* Rejected: wrong for half the year across most of the
  populated world, and the user asked specifically for DST handling.
- *`NTPClient::setTimeOffset()`.* Would shift the epoch itself, corrupting the
  value MQTT publishes and `/api/status` exposes. The epoch must stay UTC; only
  the *presentation* is local.
- *IANA zone name plus an on-device lookup table.* All the maintenance burden of
  a tzdata subset, for a friendlier stored value that the frontend can provide
  anyway (D3).

### D2. `DeviceConfig::timezone`, applied once at boot, plus live on change

The field sits next to `elevation`, which is likewise a property of where the
device physically is.

```cpp
char timezone[48] = "UTC0";   // POSIX TZ string
```

48 bytes accommodates the longest realistic rule strings (~35 characters) with
headroom. NVS key `timezone` — 8 characters, well inside the ≤12 limit
`PrefsKeys.h` documents.

`validateDeviceConfig()` falls back to `UTC0` when the string is empty or fails
`isPlausibleTimezone()`, so a corrupted NVS read degrades to UTC rather than to
undefined `tzset()` behaviour.

**Applied live, unlike most settings.** Nearly every mutating route in this
firmware saves and then calls `requestRestart(1000)`, because the setting is
consumed during `setup()`. Timezone is different: `tzset()` reads the `TZ`
environment variable and rebuilds libc's internal state, and there is no other
component holding a derived copy. Calling `applyTimezone()` in the route handler
is complete and immediate, so a restart would be pure user-visible cost for no
correctness benefit. It is also cheap — no allocation, no I/O.

The boot-time application in `setup()` is still required, for the ordinary case
where the device powers on with a stored zone.

### D3. The zone table lives in the frontend

`data/settings.html` carries a `<select>` of ~20 common zones whose `value` is
the POSIX string:

```html
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Europe/Berlin, Paris, Madrid, Rome</option>
```

**Rationale.** The device stores and applies one opaque string; it never needs
the list. Putting the table in the HTML costs zero device flash (the page is
gzipped into `src/generated/` either way, and this is a few hundred bytes
compressed), and extending or correcting the list is a frontend edit with no
firmware implications.

A **Custom** option reveals a free-text field, so any valid POSIX string —
including zones not in the curated list — remains reachable without a firmware
change. The stored value is matched back against the list on load; an unmatched
value selects Custom and populates the text field.

Deliberately excluded from the curated list: zones whose POSIX rules use the
hour-24+ extension (`Asia/Jerusalem` is `M3.4.4/26`) or which have changed
legislation repeatedly in recent years (`Africa/Cairo`). Shipping a subtly wrong
rule is worse than omitting the zone, and Custom covers those users.

### D4. Formatting helpers in `support/`, host-testable

```cpp
namespace Support {
    void   applyTimezone(const char *tz);
    bool   isPlausibleTimezone(const char *tz);
    size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch);
    size_t formatLocalDate(char *out, size_t n, uint32_t epoch);
}
```

No Arduino or FreeRTOS includes, so `+<support/LocalTime.cpp>` joins the native
`build_src_filter` alongside `Stats`, `Timer`, `NetworkWatchdog` and
`WifiBackoff`. The host's BSD libc implements the same POSIX TZ grammar as
newlib, so a native test exercising a real DST transition is testing the actual
production behaviour, not a mock.

`epoch == 0` is the established "NTP not synced" sentinel (documented in the
`mqtt-integration` spec and enforced by `getCurrentEpoch()`), so the formatters
return an empty string for it rather than printing `01:00` — the local
representation of the Unix epoch in CET, which would be actively misleading.

**On `isPlausibleTimezone()` being permissive.** The POSIX TZ grammar is more
elaborate than it looks (quoted abbreviations like `<+04>-4`, optional seconds,
fractional offsets like `IST-5:30`). A strict validator would reject valid input
and is not worth the risk. The check is therefore limited to: non-empty, fits
the buffer, printable ASCII, and at least three leading characters that could
begin a zone designation. Anything malformed past that degrades to whatever
`tzset()` makes of it, which is UTC — the same failure mode as no configuration.

## Risks / Trade-offs

- **Risk: the curated POSIX strings drift as legislation changes** (the EU has
  repeatedly discussed abolishing seasonal clock changes). → The strings live in
  editable frontend HTML, and Custom is always available. If the EU does abolish
  DST, one HTML edit fixes every European entry.
- **Risk: a user picks a zone whose rules newlib parses differently from the
  host libc**, so the native tests pass and the device is wrong. → Mitigated by
  restricting the curated list to plain POSIX forms and excluding the exotic
  ones (D3). Both implementations follow the same specification for those.
- **Trade-off: live-apply makes timezone inconsistent with every other settings
  route**, which restarts. → Accepted and documented in D2; the inconsistency is
  in the user's favour and the reasoning is recorded so it is not "fixed" later
  by adding a spurious restart.
- **Risk: `setenv`/`tzset` are not thread-safe against a concurrent
  `localtime_r`.** The route handler runs on the AsyncTCP task while the display
  formats on the Network task. → The window is microseconds and the failure mode
  is one mis-rendered clock string on a display that repaints at most once a
  minute; the next refresh is correct. Locking libc's timezone state is not
  worth it here, but the hazard is noted so a future consumer with stricter
  needs knows about it.

## Migration Plan

No data migration. A device whose NVS has no `timezone` key loads the `UTC0`
default, which reproduces today's behaviour exactly.

1. Add `LocalTime.{h,cpp}` and its native test suite; wire into
   `build_src_filter`. Verify `pio test -e native` before touching anything else.
2. Add `DeviceConfig::timezone`, the NVS key, validation, and `updateTimezone()`.
3. Apply at boot in `setup()`.
4. Switch `DisplayManager::formatClock()` to the helper.
5. Add the `/api/settings/timezone` routes.
6. Add the selector to `data/settings.html`.
7. Build, run the native suite, verify on hardware that the display footer shows
   local time and that a zone change takes effect without a restart.

**Rollback.** Revert the commits. The `timezone` key left in NVS is inert to
older firmware, and `factory-reset` clears the namespace.

## Open Questions

None blocking. One deferred: whether the device should also expose local time
via `/api/status` for the web UI to display. The frontend currently formats
times in the browser, where the user's own OS timezone applies and is usually
what they want, so there is no need yet.
