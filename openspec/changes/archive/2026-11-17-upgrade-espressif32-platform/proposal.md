# Proposal

## Why

The PlatformIO platform is pinned to `espressif32@7.1.3` — Arduino core 2.0.17,
ESP-IDF 4.4.7, mbedTLS 2.28.7, GCC 8.4. The pin is documented and working, but
nothing reports when it goes stale, so the version only moves when a human
remembers. The driver here is dependency hygiene: keep the stack current
without waiting for a forcing defect.

The upgrade was attempted and **does not fit this board**. Core 3.3.12 compiles
the firmware cleanly but adds ~31 KB of static DRAM — overwhelmingly TinyUSB
NCM/MSC/HID buffers that this firmware never uses, baked into the precompiled
framework — against an internal-heap budget of only 24.6 KB. `dram0_0_seg`
overflows by ~6.2 KB. The flash side is fine once the image is built with
`-Os`; the DRAM side cannot be bought back from application code.

So this change does the part that is safe and useful now: it lands the
**source-level compatibility work** that core 3.x requires. Those changes are
verified to build and link on the current 7.1.3 platform — the resulting image
is byte-for-byte the same size as the shipped one — so they carry no runtime
risk today, and they remove the migration work from the eventual bump.

## What Changes

- Move `Network` out of the global namespace: `src/Network.h/.cpp` become
  `src/network/Network.h/.cpp` and the class becomes `Net::Network`. Core 3.x
  ships a `Network` library whose header *and* global object collide with a
  global `Network` here; without this, every translation unit that includes
  `<WiFi.h>` fails to compile.
- Replace the format arguments that break under the new toolchain with casts
  (`static_cast<unsigned>(...)`), rather than widening the specifiers. This is
  a no-op on the current compiler, where `uint32_t` is `unsigned int`, and it
  is what the new compiler needs, where `uint32_t` is `unsigned long`.
- Restore `I2CDevice` as an aggregate (C++20 no longer treats a user-declared
  `= default` constructor as aggregate-compatible) and replace
  `WiFiClass::status()` with `WiFi.status()` (`status()` stopped being static).
- Keep the platform pinned at 7.1.3, and record *why* in the pin comment,
  pointing at the DRAM analysis, so the next person does not repeat it blind.
- Correct the `system-architecture` memory-budget requirement, whose flash
  ceiling disagreed with `partitions.csv` and CI (1900 KB vs 1856 KB), and
  record that heap thresholds must be revalidated whenever the core generation
  moves.

## Capabilities

### New Capabilities

None. This change preserves behaviour; it prepares the source for a future
toolchain without changing what the device does.

### Modified Capabilities

- `system-architecture`: the *Memory budget* requirement. Restates the flash
  ceiling as the enforced 1856 KB app partition instead of the two conflicting
  figures in the text, and adds a scenario requiring the internal-heap
  thresholds to be revalidated and recorded after a toolchain-generation
  change — which is the obligation the attempted bump exposed.

## Impact

- `src/network/Network.h`, `src/network/Network.cpp` — moved, class now
  `Net::Network`, documented at the top of the header.
- ~20 source files — qualified `Net::Network` references, format-argument
  casts, `WiFi.status()`, one aggregate fix.
- `platformio.ini` — platform stays `7.1.3`; the comment gains the measured
  blocker and the state of the source-compatibility work.
- `openspec/specs/system-architecture/spec.md` — via the delta above.
- No dependency, partition, or build-flag changes.
