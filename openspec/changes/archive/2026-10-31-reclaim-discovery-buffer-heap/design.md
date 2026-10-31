# Design

## Context

Change `2026-10-31-reduce-heap-churn` traded a transient 2048 B stack buffer
for a permanent 2048 B `.bss` buffer in `MqttPublisher::publishDiscovery()`. Its
`tasks.md` recorded the cost (`dram0.bss 64096 -> 66144`) but framed it as
neutral. It is not: on this board the heap region begins where BSS ends, so the
buffer is a permanent 2 KB deduction from the resource being optimised.

## Decisions

### 1. Put the discovery payload back on the Network task stack

`firmware.map` shows `_bss_end` and `_heap_start` resolving to the same address
(`0x3ffd87d0`), so `.bss` growth is heap shrinkage. `publishDiscovery()` is
called only from the MQTT connect path after a station association; the
AP/captive-portal path never reaches it. The Network task (`src/Network.cpp`)
allocates 8192 B and its measured peak is 3544 B, leaving ≈4.6 KiB — more than
enough for the 2048 B buffer while the publish is formatted.

Keeping the buffer on the stack makes it free in steady state: it exists only
while the retained discovery message is built and published, then the stack
frame is reused.

### 2. Size the response stream to exactly `measureJson()`

`AsyncResponseStream(contentType, bufferSize)` constructs a `cbuf(bufferSize)`,
and `cbuf` itself reserves a sentinel byte (`_size = size + 1`,
`new char[size + 1]`), so `room()` for `bufferSize == len` is exactly `len`.
The prior `len + 1` therefore allocated `len + 2` bytes and left one unused.
Passing `len` writes the whole document with no resize and no slack.

## Alternatives considered

- Keep the static buffer but shrink it below 2048 B: rejected — the worst-case
  payload is 1787 B (1828 B framed), so any permanent buffer is still ~1.75 KB
  of heap for a value that is only needed for one publish.
- Heap-allocate the buffer at publish time and free it after: rejected — it
  reintroduces the per-connect heap churn the original change was trying to
  remove, and can fragment the heap on a path that already allocates the
  PubSubClient TX buffer.
