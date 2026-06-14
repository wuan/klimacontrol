## Context

The legacy `specs/NN-*.md` collection was added in commit `6728a2a` ("add openspec specs") with a different OpenSpec config format (JSONPath rules from older `@fission-ai/openspec` versions). The validation infrastructure for that format never made it into CI — the active GitHub Action only checked filenames. Meanwhile the firmware grew (sensor variants added, WiFi resilience overhaul, etc.) and the legacy specs drifted away from the code. The `wifi-resilience` change introduced the spec-driven schema used by the current `openspec` CLI (`### Requirement` + `#### Scenario` WHEN/THEN), and this migration consolidates everything on that one schema.

## Goals / Non-Goals

**Goals:**
- One spec file per capability, in the spec-driven schema the current CLI validates.
- Preserve the substance of the legacy specs (what the firmware does today).
- Keep `network-wifi-resilience` (just archived) intact and avoid duplicating its requirements in the new `networking` capability.
- End state: `openspec validate --all --strict` passes on the resulting collection.

**Non-Goals:**
- Documenting features that don't exist yet. Legacy `FUTURE:` items (configurable hysteresis, integral-windup follow-ups, automatic integral reset on disable, etc.) are dropped — they belong in future proposals, not in this change.
- Rewriting the firmware to match the specs. Where the legacy spec and the code disagree, the new spec follows the code.
- Migrating any other documentation in `specs/` outside the 10 numbered files plus `spec.md`.

## Decisions

**One umbrella change instead of 10 small changes.**
All 10 capabilities are added in a single `baseline-capabilities` change rather than ten back-to-back changes. Each capability is still its own delta-spec file, but they archive together.

*Why:* They share a single motivation (legacy migration) and the entire batch is meaningful only together. Ten separate changes would produce ten "Initial documentation of X" entries in the archive — noise without information.

**Capability naming follows function, not file index.**
`01-system-architecture.md` → `system-architecture`. `03-api-specification.md` → `http-api` (more specific than the generic "API"). `04-networking.md` → `networking` (scoped, since `network-wifi-resilience` is its own capability).

*Why:* The new collection is queried by capability name (`openspec show <name>`), not by index. Names like `01-system-architecture` would tie the spec to its legacy ordinal forever.

**Drop `FUTURE:` items.**
Legacy entries marked `FUTURE: (not implemented)` are excluded from the migration.

*Why:* Specs in the new schema are testable assertions about current behavior — every `### Requirement` must have a `#### Scenario` with concrete WHEN/THEN. Future plans don't satisfy that. They can be re-introduced as new proposals when the work is actually scheduled.

**Networking spec scope: AP/STA setup, mDNS, NTP, captive portal, AP-fallback policy. Excluded: reconnect/retry behavior.**
The legacy `S4.10` ("SHALL auto-reconnect") and the AP-fallback boot policy stay in `networking`. The mid-session recovery details (event handler, in-session retries, active reconnect, deep-state restart) live entirely in `network-wifi-resilience`.

*Why:* Avoids duplicate requirements that would need to be kept in sync. The boundary maps cleanly to source: `Network::startSTA()` and `Network::startAP()` shape `networking`; `Network::onWiFiEvent()` and the active-reconnect block shape `network-wifi-resilience`.

## Risks / Trade-offs

- **Spec/code drift on the way back in.** The legacy specs already lagged the code by several months; the same pattern can repeat. → Mitigation: the new CI step (`openspec validate --all --strict`) only validates schema, not implementation correspondence. The real defense is making subsequent changes go through the propose/design/specs/tasks flow so spec updates ride alongside the code.

- **Some legacy scenarios were vague.** Examples: `S9.8` ("ON SHALL display solid color (implementation-defined)") doesn't anchor to a specific color. Where the original was vague, the new spec says what the code actually does. → Mitigation: if reality differs from what I wrote, file a follow-up; the spec is the contract going forward.

- **Single umbrella change is large.** Reviewing ~10 spec files at once is harder than one at a time. → Mitigation: the per-capability files are independent and reviewable individually within the change folder. A reviewer can read one capability without needing context from the others.

- **The `spec.md` overview is dropped.** Anyone who relied on it as a hand-curated TOC loses it. → Mitigation: `openspec list --specs` is the canonical TOC in the new system, and (once published docs are rewired) the GitHub Pages index page can be regenerated from the live collection.
