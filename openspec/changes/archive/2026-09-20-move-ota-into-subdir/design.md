## Context

The `src/` tree already organises most subsystems into per-domain
subdirectories — `control/`, `sensor/`, `actuator/`, `display/`,
`network/`, `routes/`, `support/`. The OTA subsystem is the exception:
its `.cpp`/`.h`/`config` files live at the top of `src/` next to
unrelated core files (`Config.cpp`, `MqttClient.cpp`, `StatusLed.cpp`,
etc.), and the two OTA-specific support headers
(`support/RedirectScheme.h`, `support/VersionCompare.h`) live under
`support/` rather than with the rest of OTA.

The OTA subsystem has grown: the `ota-transport-hardening` change added
`RedirectScheme.h`, a `Support::isExpectedFirmwareAsset` helper inside
`OTAConfig.h`, and a 19-case native test suite (`test_ota_transport`).
Five files now claim the name "OTA" — the three top-level files plus
two headers that only OTA consumes. Continuing to keep them scattered
makes it harder to spot the boundary, and means a future reader who
opens `src/OTAUpdater.cpp` doesn't see `RedirectScheme.h` or
`VersionCompare.h` in the obvious nearby place.

## Goals / Non-Goals

**Goals:**

- All OTA-related files (`OTAUpdater.h`, `OTAUpdater.cpp`, `OTAConfig.h`,
  `RedirectScheme.h`, `VersionCompare.h`) live under `src/ota/`.
- Include directives in callers updated to match the new paths.
- `pio run -e adafruit_qtpy_esp32s2` and `pio test -e native` both
  continue to pass with no warnings.
- `openspec validate --all --strict` continues to pass.

**Non-Goals:**

- Renaming the files themselves (`OTAUpdater.h` stays
  `OTAUpdater.h`, not `Updater.h`).
- Renaming the include guards (they use the filename root, which
  doesn't change).
- Splitting `OTAConfig.h` (the firmware-version macros are used by
  `display/EPaperDisplay.cpp` and could be argued to belong in a
  separate `FirmwareVersion.h`, but that is a wider rename question
  the user has not asked for — `OTAConfig.h` is its own file and
  moving it is a clean, reversible step).
- Moving the OTA-only files *out* of `ota/` later when something
  non-OTA needs `RedirectScheme.h` or `VersionCompare.h`. If and when
  that happens it is a separate decision; today they have exactly one
  caller each.
- Changing public API or observable behaviour.

## Decisions

### Decision 1: `src/ota/` mirrors the existing per-subsystem pattern

**Choice.** Create `src/ota/` and move the five files in. Update
include paths.

**Rationale.** `src/control/`, `src/sensor/`, `src/actuator/`,
`src/display/`, `src/network/`, `src/routes/` already use this
pattern. The build system (PlatformIO + Arduino) auto-discovers
sources by extension, so a subdirectory introduces no special
handling. The `native` build's `build_src_filter` does not list
`OTAUpdater.cpp` (it is `#ifdef ARDUINO`-guarded), so the filter does
not need updating either.

**Alternatives considered.** *Keep everything at `src/` top-level
under a `OTA_` prefix.* Already the case; the proposal is to invert
that. *Move into `src/network/`.* Conceptually wrong — OTA is a
peer-level subsystem to networking, not a sub-component of it.

### Decision 2: Move `RedirectScheme.h` and `VersionCompare.h` too

**Choice.** The two OTA-only `support/` headers move into `src/ota/`
with the rest of OTA, not stay behind in `support/`.

**Rationale.** They are used by exactly one subsystem (`OTAUpdater.cpp`
for `RedirectScheme.h`, `OTAUpdater.cpp` and `test_ota_updater.cpp`
for `VersionCompare.h`). Keeping them in `support/` while everything
else OTA moves would leave an inconsistent cut: half of OTA in
`src/ota/`, half across the tree. The cost of leaving them is
discoverability — a reader scanning `src/ota/` would miss them.
The benefit of leaving them is "they are styled like `support/`
files." That style argument is weak; the existing `support/` headers
(`Stats.h`, `Timer.h`, `RequestDiag.h`, …) are general-purpose
utilities consumed by multiple subsystems, not OTA-only.

**Alternatives considered.** *Leave both in `support/`.* Inconsistent
boundary; future readers won't know they are OTA-only. *Move only
`RedirectScheme.h` (the most recently added).* Even more
inconsistent.

### Decision 3: No header-guard rename

**Choice.** Keep the existing guards (`KLIMACONTROL_OTA_UPDATER_H`,
`KLIMACONTROL_OTA_CONFIG_H`, `KLIMACONTROL_REDIRECT_SCHEME_H`,
`KLIMACONTROL_VERSION_COMPARE_H`).

**Rationale.** The guards already key off the *filename root*, not
the *path*. `src/ota/OTAUpdater.h` and `src/OTAUpdater.h` both
produce the same guard, so the rename is free. Renaming would only
buy us a guard that mentions `OTA_` in a way that the existing ones
already do.

**Alternatives considered.** *Rename to `KLIMACONTROL_OTA_*`.* Pure
churn.

### Decision 4: Display module still includes `ota/OTAConfig.h` for `FIRMWARE_VERSION`

**Choice.** `src/display/EPaperDisplay.cpp`'s `#include "OTAConfig.h"`
becomes `#include "ota/OTAConfig.h"`.

**Rationale.** `FIRMWARE_VERSION` is a single source of truth, and
`OTAConfig.h` is where it lives. Splitting it into a separate
`FirmwareVersion.h` is a wider rename question (where would the
build-time `__DATE__`/`__TIME__` macros go?) and is out of scope for
this change. The display module already reads other OTA-defined
constants indirectly (via the OTA JSON), so reading one constant
from `ota/` is no surprise.

**Alternatives considered.** *Split `OTAConfig.h` into
`FirmwareVersion.h` + `OTAConfig.h`.* Out of scope; would touch
many consumers and create a circular question about where build-time
identity belongs.

## Risks / Trade-offs

- **Missed include site.** [Risk: an `#include "OTAUpdater.h"` or
  `#include "OTAConfig.h"` somewhere is missed and the build breaks
  in a confusing place.] → Mitigation: search the entire `src/` and
  `test/` trees for both quoted forms before declaring the move done;
  the firmware build (`pio run -e adafruit_qtpy_esp32s2`) and the
  native tests (`pio test -e native`) both compile and link all the
  consumers, so a missed site surfaces immediately.

- **`#include "support/VersionCompare.h"` left dangling.** [Risk: a
  test or source still includes the old path after the move.] →
  Mitigation: grep for both `"support/RedirectScheme.h"` and
  `"support/VersionCompare.h"` before declaring done; both must
  return zero matches.

- **Header-guard collision.** [Risk: moving the header keeps the
  guard name, so two translation units including the new and old
  paths during a half-completed move both succeed at the guard, and
  the wrong copy is included.] → Mitigation: do the move atomically
  — do not leave both old and new files on disk simultaneously. The
  implementation tasks are ordered so each file is moved before the
  consumers are updated, but no file is moved before its consumers
  stop referencing the old path. (For files with no consumers in
  this repo — e.g. `VersionCompare.h` is only included by tests and
  OTAUpdater.cpp — the move is safe at any point.)

## Migration Plan

1. Update all `#include` sites that reference the about-to-be-moved
   files (`OTAUpdater.h`, `OTAConfig.h`,
   `support/RedirectScheme.h`, `support/VersionCompare.h`) to point
   at the new paths. This is safe to do first: the old files still
   exist, and a mix of new and old paths is harmless.
2. Move the five files (`git mv`) into `src/ota/`.
3. Run `pio run -e adafruit_qtpy_esp32s2` and `pio test -e native`
   to confirm both still build and pass.
4. Run `openspec validate --all --strict` to confirm the spec is
   unchanged.

Rollback: revert the commit. No runtime state changes, so a rollback
is safe at any point.

## Open Questions

None. The move is mechanical; the only judgement calls (whether
to move the two `support/` headers, whether to split
`OTAConfig.h`) were surfaced to the user before this design was
written.
