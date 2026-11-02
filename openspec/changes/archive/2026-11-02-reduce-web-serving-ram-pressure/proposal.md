# Proposal

## Why

Serving the web UI is the largest transient on the device's internal SRAM, and
it is not the asset storage. On-device profiling (fw v0.1.27) showed the
gzipped assets live in flash (DROM) and cost no RAM. What costs RAM is the TCP
path of each in-flight response: lwIP `TCP_SND_BUF_DEFAULT = 5760` plus
`TCP_WND_DEFAULT = 5760` (both precompiled into the Arduino core) plus
ESPAsyncWebServer's 2.8 KiB response send buffer, roughly 12 KiB of internal
SRAM per active connection.

A browser loading one page opens three connections — the HTML, `common.css`
and `favicon.svg` — so a single page view multiplies that footprint by three.
Measured on the test device (~42 KiB internal heap free at idle):

- one realistic page load (`/settings` + `/common.css` + `/favicon.svg`):
  ~17 KiB peak, `min_free_heap` down to 25 KiB;
- 3 simultaneous `/settings` loads: ~37 KiB peak, `min_free_heap` down to ~5 KiB;
- 10 simultaneous `/settings` loads: internal free heap held near 12 KiB until
  the low-heap guard counted five consecutive low samples and **restarted the
  device** (`net CRITICAL: Low heap persisted - restarting...`).

The guard already protects against the crash, but a restart is the symptom.
Removing the connection fan-out is the fix, and it costs only flash (469 KiB
free), which is not scarce.

## What Changes

- Every page becomes a single self-contained response: the shared styles and
  the icon are inlined, so loading a page issues exactly one HTTP request
  instead of three. The per-page-load peak internal-heap cost drops to one
  connection's send path.
- Inlining happens at build time in `scripts/compress_web.py`, which already
  turns `data/*.html` into gzipped headers. `data/common.css` and
  `data/favicon.svg` stay the single source of truth; the build replaces each
  page's `<link rel="stylesheet" href="/common.css">` with an inline `<style>`
  block and its `<link rel="icon" ...>` with a `data:` URI before minifying
  and compressing.
- The `/common.css` and `/favicon.svg` HTTP routes stop being referenced by any
  page. They are removed once clients that cached the previous page (the
  response sets `Cache-Control: max-age=86400`) are expected to have fetched
  the new HTML; the design note records the transition.

## Capabilities

### New Capabilities

- None.

### Modified Capabilities

- `web-interface` — new requirement: each page load is a single self-contained
  request (no external stylesheet/icon sub-requests).
- `memory-management` — new requirement: a single page load stays above the
  low-heap guard threshold and returns the heap to its pre-request level, so
  the connection fan-out cannot drive the guard to restart.

## Impact

- `scripts/compress_web.py` (build-time inlining), `data/*.html`
  (`<link>` tags become inlining markers), `data/common.css` / `data/favicon.svg`
  (unchanged sources), `src/WebServerManager.cpp` (`setupCommonRoutes` routes),
  and the generated headers in `src/generated/`.
- No API or config changes. No new dependency. Flash grows by roughly the
  inlined CSS/icon per page; internal RAM is unchanged for a single connection
  and falls for a page load.
- Verification is on hardware (heap instrumentation over HTTP), because the
  affected code only compiles under `ARDUINO`.
