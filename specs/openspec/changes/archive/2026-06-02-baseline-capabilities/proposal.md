## Why

The repository has two parallel specification systems: the legacy `specs/NN-*.md` collection (hand-maintained markdown with `**SX.Y**` numbered requirements, validated only by a filename regex) and the new spec-driven `specs/openspec/` collection (started with the `wifi-resilience` change). Keeping both leads to drift — the legacy networking spec already misses the resilience requirements added in `wifi-resilience`. This change migrates the legacy content into the new schema so there is exactly one source of truth per capability.

## What Changes

- Translate the 10 legacy specs (`specs/01-…10-*.md`) into per-capability spec files under the new openspec collection.
- Each legacy file maps to a single new capability with the `### Requirement: <name>` + `#### Scenario: <name>` (WHEN/THEN) schema.
- `network-wifi-resilience` (already archived) covers reconnect/retry behavior — the new `networking` capability is scoped to exclude those concerns and only documents AP/STA basics, mDNS, NTP, and captive portal.
- `FUTURE:` items in the legacy specs (unimplemented hysteresis, integral-windup follow-ups, etc.) are intentionally **dropped** — this change documents what the firmware does today, not aspirational features.
- The legacy `specs/spec.md` overview/TOC is dropped — `openspec list --specs` plays that role in the new system.

## Capabilities

### New Capabilities

- `system-architecture`: hardware platform, FreeRTOS task model, memory constraints, ownership hierarchy.
- `sensor-management`: supported sensor types, sensor lifecycle, status tracking, measurement types, calculated values.
- `http-api`: REST endpoints under `/api/` + page routes, response shape, framework constraints.
- `networking`: AP and STA mode setup, mDNS advertisement, NTP sync, captive portal, AP-fallback policy. **Excludes** disconnect recovery, which lives in `network-wifi-resilience`.
- `ota-updates`: GitHub-release-based OTA mechanism, memory safety, partition management, rollback.
- `mqtt-integration`: MQTT client config, connection management, publish topic format, LWT.
- `web-interface`: embedded HTML/JS/CSS, dashboard polling model, settings modal layout.
- `configuration`: NVS-backed configuration storage, structs per domain, partial-update API, factory reset.
- `status-led`: NeoPixel state machine and color semantics.
- `temperature-control`: PID-based controller, setpoint and limits, integral-windup prevention.

### Modified Capabilities

None. `network-wifi-resilience` is already in place and intentionally left unchanged.

## Impact

- **Specs**: 10 new canonical spec files appear under `specs/openspec/specs/<capability>/spec.md` after archive.
- **Files removed**: `specs/spec.md` and `specs/01-…10-*.md` (11 files) — handled in a follow-up cleanup step, not by this change directly.
- **Tooling**: The OpenSpec CI workflow (`.github/workflows/openspec.yml`) already validates the new collection; nothing to wire up. The legacy filename-regex check in `scripts/validate-openspec.sh` and the legacy HTML rendering in `.github/workflows/deploy-pages.yml` and `package.json` become obsolete and are updated separately.
- **Code**: No firmware code changes. This is a documentation migration.
