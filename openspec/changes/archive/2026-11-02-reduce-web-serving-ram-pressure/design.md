# Design

## Context

See `proposal.md` — Why, for the measured heap data and motivation.

Current state that shapes the approach:

- `scripts/compress_web.py` runs as a PlatformIO pre-build hook and as a
  standalone script. It globs `data/*.html`, `data/*.css`, `data/*.svg`,
  minifies each independently, gzips it, and emits `src/generated/<name>_gz.h`
  (`PROGMEM` byte arrays).
- `src/WebServerManager.cpp::setupCommonRoutes()` registers `/common.css`
  (from `common_gz.h`) and `/favicon.svg` (from `favicon_gz.h`); every page
  references both via `<link>` tags in its `<head>`.
- Responses are sent with `Cache-Control: max-age=86400`.

## Goals / Non-Goals

**Goals:**

- A page load issues exactly one HTTP request, so its peak internal-SRAM cost
  is one response's send path (see `specs/memory-management/spec.md`).
- `data/common.css` and `data/favicon.svg` stay the single source of truth;
  no copy-paste of stylesheet text into four HTML files.

**Non-Goals:**

- Shrinking or replacing the gzipped assets (flash is not scarce). A proper
  minifier was measured (`html-minifier-terser`: ~1.5 KiB / 7 % smaller
  gzipped, zero RAM effect) and is deliberately deferred to a separate
  build-tooling change, since it adds a Node dependency to the build.
- Changing the lwIP `TCP_SND_BUF`/`TCP_WND` values or the ESPAsyncWebServer
  send buffer (precompiled, see proposal).
- Any change to the HTTP JSON API.

## Decisions

**1. Inline at build time in `compress_web.py`, not by hand in the HTML.**

Before minifying each `.html`, the script replaces the page's
`<link rel="stylesheet" href="/common.css">` with `<style>{minify_css(common.css)}</style>`
and its `<link rel="icon" ... href="/favicon.svg">` with a `data:` URI. The
`<link>` tags stay in the HTML sources as stable sentinels.

- *Alternative — hand-inline into each page:* rejected; four copies of the CSS
  drift apart.
- *Alternative — runtime template substitution:* rejected; adds per-request
  work and heap churn, the opposite of the goal.
- *Safety:* after processing, the build asserts no `href="/common.css"` or
  `href="/favicon.svg"` remains and that the sentinels were present. A page
  that forgets them fails the build instead of silently fanning out again.

**2. Encode the icon as a percent-encoded `data:image/svg+xml,` URI, not base64.**

The SVG is ~1.6 KiB of text. Percent-encoding keeps it text, so gzip still
compresses it (adds well under 1 KiB per page gzipped); base64 expands by ~33 %
and gzips poorly (~2.2 KiB per page). Escaping `<`, `>`, `#`, `"`, `%` is
sufficient for the icon's content.

**3. Keep the `/common.css` and `/favicon.svg` routes for one transition.**

The HTML itself is cached for 24 h. A returning client can therefore hold a
pre-change page that still references `/common.css` for up to a day after the
firmware updates. Removing the routes immediately would leave that page
unstyled until its HTML cache expires. The routes are kept (≈3 KiB flash,
unreferenced by any new page) and their removal is deferred; the inlining is
what removes the *connections*, which is the point.

**4. Recompute the icon/CSS exactly as the existing per-file path does.**

The inlined CSS goes through the same `minify_css()` that produces
`common_gz.h` today, so the bytes on the wire for the stylesheet are
unchanged — only their delivery (inline vs. separate request) differs.

## Risks / Trade-offs

- [Flash grows by the inlined CSS/icon per page, ≈10–15 KiB total] → Flash has
  469 KiB free; the trade is deliberate and one-directional (RAM is the scarce
  resource).
- [A cached pre-change page references `/common.css` after OTA] → Routes are
  kept (decision 3).
- [A page's sentinel is edited away and silently stops inlining] → Build-time
  assertion (decision 1).
- [Percent-encoded data URI mis-parsed by an old browser] → Only costs the tab
  icon; the page still renders. Acceptable.
- [Inlining grows the settings HTML past one TCP window, so it stays chunked] →
  Per-connection peak is already bounded by `TCP_SND_BUF`+`TCP_WND`, not by page
  size, so the single-connection peak is unchanged.

## Migration Plan

1. Add inlining + assertions to `compress_web.py`; regenerate headers.
2. Keep `setupCommonRoutes()` and the `common_gz.h` / `favicon_gz.h` generation
   as-is.
3. Build, flash, and verify on hardware: a page load issues one request, and
   `min_free_heap` stays above the 16 KiB guard threshold for a single
   `/settings` load.
4. Rollback: revert `compress_web.py` and regenerate; the routes and assets are
   untouched, so a revert restores the previous behavior exactly.
5. Later change (not now): remove the `/common.css` and `/favicon.svg` routes
   and their header generation once cached clients have aged out.

## Open Questions

- When to remove the legacy `/common.css` and `/favicon.svg` routes — safe to
  decide after a release, and it does not affect this design or the specs.
