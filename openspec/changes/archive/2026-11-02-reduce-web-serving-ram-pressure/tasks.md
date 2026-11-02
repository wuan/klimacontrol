# Tasks

## 1. Build-time inlining

- [x] 1.1 In `scripts/compress_web.py`, before minifying each `.html`, replace
  the page's `<link rel="stylesheet" href="/common.css">` with an inline
  `<style>` block built from `minify_css(common.css)`. Verify by running
  `python3 scripts/compress_web.py` and confirming the regenerated
  `src/generated/control_gz.h`, `settings_gz.h`, `about_gz.h`, and
  `config_gz.h` are larger than before and the script exits 0.
- [x] 1.2 Replace each page's `<link rel="icon" type="image/svg+xml"
  href="/favicon.svg">` with a percent-encoded
  `data:image/svg+xml,` URI of the minified icon. Verify the processed page
  text contains `data:image/svg+xml,` and no `href="/favicon.svg"`.
- [x] 1.3 Add build assertions: each `.html` must contain both `<link>`
  sentinels, and after processing no `href="/common.css"` or
  `href="/favicon.svg"` may remain. Verify by temporarily deleting one
  sentinel from a page and confirming the script exits non-zero with a
  message naming the file, then restoring it.
- [x] 1.4 Reuse the existing `minify_css()` for inlining (same function that
  produces `common_gz.h`), and assert that the inlined stylesheet appears
  exactly once per page so a duplicate sentinel cannot inflate flash. Verify by
  temporarily adding a second `common.css` sentinel to a page and confirming
  the build fails, then restoring it.

## 2. Pages and routes

- [x] 2.1 Confirm all four pages (`control.html`, `settings.html`,
  `about.html`, `config.html`) keep the sentinel `<link>` tags and are covered
  by the inlining; verify the task 1.3 assertion passes for every page.
- [x] 2.2 Keep `WebServerManager::setupCommonRoutes()` and the
  `common_gz.h` / `favicon_gz.h` generation in place. Verify a direct
  `GET /common.css` and `GET /favicon.svg` still return HTTP 200 on hardware,
  so a client holding a cached pre-change page still renders.

## 3. Hardware verification

- [x] 3.1 Build the firmware with `pio run -e adafruit_qtpy_esp32s2` and
  confirm it compiles cleanly with `-Werror`.
- [x] 3.2 Flash the connected device and load each page in a browser (or with
  a fresh `curl` per page); verify from the serial `http` log that each page
  load issues exactly one request and no `GET /common.css` or
  `GET /favicon.svg` follows.
- [x] 3.3 On the idle device, serve one `GET /settings` and read
  `GET /api/about`; verify `min_free_heap` stays above 16384 and `free_heap`
  returns to its pre-request level, then load the page repeatedly and verify
  `free_heap` does not drift downward.
- [x] 3.4 Run `openspec validate --all --strict` (or
  `scripts/validate-openspec.sh`) and confirm it passes.

## Workflow follow-up

- Archive the change once the review requirements are satisfied.
- Deferred follow-up (separate change): remove the `/common.css` and
  `/favicon.svg` routes and the `common_gz.h` / `favicon_gz.h` generation once
  clients holding a cached pre-change page have aged out.
