## 1. Capability specs

- [x] 1.1 Convert `specs/01-system-architecture.md` → `specs/system-architecture/spec.md`.
- [x] 1.2 Convert `specs/02-sensor-management.md` → `specs/sensor-management/spec.md`.
- [x] 1.3 Convert `specs/03-api-specification.md` → `specs/http-api/spec.md`.
- [x] 1.4 Convert `specs/04-networking.md` → `specs/networking/spec.md`, scoped to exclude reconnect/retry (covered by `network-wifi-resilience`).
- [x] 1.5 Convert `specs/05-ota-updates.md` → `specs/ota-updates/spec.md`.
- [x] 1.6 Convert `specs/06-mqtt-integration.md` → `specs/mqtt-integration/spec.md`.
- [x] 1.7 Convert `specs/07-web-interface.md` → `specs/web-interface/spec.md`.
- [x] 1.8 Convert `specs/08-configuration.md` → `specs/configuration/spec.md`.
- [x] 1.9 Convert `specs/09-status-led.md` → `specs/status-led/spec.md`.
- [x] 1.10 Convert `specs/10-temperature-control.md` → `specs/temperature-control/spec.md`.

## 2. Validate and archive

- [ ] 2.1 Run `openspec validate --all --strict --no-interactive` from the project root; confirm 11 items pass (10 new + `network-wifi-resilience`).
- [ ] 2.2 Archive the change: `openspec archive baseline-capabilities --yes` (run from `specs/`).
- [ ] 2.3 Confirm 10 canonical specs appear under `specs/openspec/specs/`, plus the existing `network-wifi-resilience`.

## 3. Remove legacy files and update references

- [ ] 3.1 `git rm specs/spec.md specs/0[0-9]-*.md specs/1[0-9]-*.md`.
- [ ] 3.2 Update `package.json` scripts to point at the new collection (`validate:openspec` → `openspec validate --all --strict`; drop or rewrite `generate:openspec-docs`).
- [ ] 3.3 Replace the body of `scripts/validate-openspec.sh` with a single call to `openspec validate --all --strict --no-interactive` (drop the legacy filename-regex check).
- [ ] 3.4 Update the OpenSpec rendering block in `.github/workflows/deploy-pages.yml` to either render the new collection or remove the spec section if no public render is needed.
