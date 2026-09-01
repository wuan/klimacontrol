# local-time Specification Delta

## MODIFIED Requirements

### Requirement: Local time formatting helpers

The firmware SHALL provide, in a translation unit free of Arduino and FreeRTOS
dependencies so it builds and is tested in the `native` environment:

- `size_t formatLocalHhMm(char *out, size_t n, uint32_t epoch)` — writes `HH:MM`
- `size_t formatLocalDate(char *out, size_t n, uint32_t epoch)` — writes `YYYY-MM-DD`
- `size_t formatLocalDateHhMm(char *out, size_t n, uint32_t epoch)` — writes
  `YY-MM-DD HH:MM`, i.e. a two-digit year, for callers that must fit the whole
  date and time on one line beside other content. Needs a 15-byte buffer.

All SHALL write an empty string and return 0 when `epoch` is 0, which is the
established "NTP not yet synced" sentinel. All SHALL tolerate a null buffer or a
zero-length buffer without writing.

#### Scenario: Unsynced clock renders nothing

- **WHEN** `formatLocalHhMm()` is called with an epoch of 0
- **THEN** it SHALL write an empty string and return 0, rather than rendering the local representation of the Unix epoch

#### Scenario: Combined date and time uses a two-digit year

- **WHEN** `formatLocalDateHhMm()` is called with an epoch resolving to 2026-01-15 13:00 local
- **THEN** it SHALL write `26-01-15 13:00`

#### Scenario: Combined formatter honours the unsynced sentinel

- **WHEN** `formatLocalDateHhMm()` is called with an epoch of 0
- **THEN** it SHALL write an empty string and return 0

#### Scenario: Null and zero-length buffers

- **WHEN** any formatter is called with a null pointer, or with a buffer length of 0
- **THEN** it SHALL return 0 and SHALL NOT write to memory
