#!/usr/bin/env python3
"""
Build script to compress HTML files for ESP32 web server.
Minifies and gzip compresses HTML/CSS/JS, then generates C++ header files.

Can be run standalone or as a PlatformIO pre-build script.
"""

import gzip
import re
import subprocess
import sys
from pathlib import Path

# Handle PlatformIO environment
try:
    Import("env")
    PLATFORMIO_BUILD = True
    # In PlatformIO context, get project dir from env
    PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
except:
    PLATFORMIO_BUILD = False
    # When run standalone, derive from script location
    SCRIPT_DIR = Path(__file__).parent
    PROJECT_DIR = SCRIPT_DIR.parent

DATA_DIR = PROJECT_DIR / "data"
GENERATED_DIR = PROJECT_DIR / "src" / "generated"


def minify_css(css: str) -> str:
    """Minify CSS content."""
    # Remove CSS comments
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.DOTALL)
    # Remove extra whitespace
    css = re.sub(r'\s+', ' ', css)
    # Remove spaces around special characters
    css = re.sub(r'\s*([{};:,>~+])\s*', r'\1', css)
    # Remove trailing semicolons before closing braces
    css = re.sub(r';}', '}', css)
    return css.strip()


def minify_html(html: str) -> str:
    """Simple HTML/CSS/JS minifier."""
    # Remove HTML comments (but preserve IE conditional comments)
    html = re.sub(r'<!--(?!\[if).*?-->', '', html, flags=re.DOTALL)

    # Minify inline CSS
    def minify_inline_css(match):
        css = match.group(1)
        return f'<style>{minify_css(css)}</style>'

    html = re.sub(r'<style[^>]*>(.*?)</style>', minify_inline_css, html, flags=re.DOTALL | re.IGNORECASE)

    # Minify inline JavaScript (conservative - only remove comments and collapse whitespace)
    # Note: Aggressive operator-space removal breaks template literals containing HTML/CSS
    def minify_js(match):
        js = match.group(1)
        # Remove single-line comments (but not URLs with //)
        js = re.sub(r'(?<!:)//(?!/).*?$', '', js, flags=re.MULTILINE)
        # Remove multi-line comments
        js = re.sub(r'/\*.*?\*/', '', js, flags=re.DOTALL)
        # Collapse multiple spaces/newlines to single space
        lines = js.split('\n')
        minified_lines = []
        for line in lines:
            line = line.strip()
            if line:
                minified_lines.append(line)
        js = ' '.join(minified_lines)
        # Collapse multiple spaces to single space
        js = re.sub(r' +', ' ', js)
        return f'<script>{js.strip()}</script>'

    html = re.sub(r'<script[^>]*>(.*?)</script>', minify_js, html, flags=re.DOTALL | re.IGNORECASE)

    # Remove whitespace between tags
    html = re.sub(r'>\s+<', '><', html)

    # Collapse multiple whitespace to single space
    html = re.sub(r'\s+', ' ', html)

    # Remove leading/trailing whitespace
    html = html.strip()

    return html


def minify_svg(svg: str) -> str:
    """Minify SVG content (collapse whitespace)."""
    return re.sub(r'\s+', ' ', svg).strip()


# Sentinels kept in data/*.html. The build replaces each with an inlined
# asset so a page load issues exactly one HTTP request (no /common.css or
# /favicon.svg sub-request). See change reduce-web-serving-ram-pressure.
COMMON_CSS_LINK = '<link rel="stylesheet" href="/common.css">'
FAVICON_LINK = '<link rel="icon" type="image/svg+xml" href="/favicon.svg">'


def svg_data_uri(svg: str) -> str:
    """Percent-encode an SVG into a data: URI usable in an HTML attribute.

    Percent-encoding (not base64) keeps the icon as text so gzip still
    compresses it. Only the characters that would break a URI or the
    double-quoted attribute are escaped.
    """
    escapes = {
        '%': '%25',
        '#': '%23',
        '<': '%3C',
        '>': '%3E',
        '"': '%22',
        '&': '%26',
        "'": '%27',
        ' ': '%20',
        '\t': '%09',
        '\n': '%0A',
        '\r': '%0D',
    }
    return 'data:image/svg+xml,' + ''.join(escapes.get(ch, ch) for ch in svg)


def inline_page_assets(html: str, common_css: str, favicon_uri: str, page_name: str) -> str:
    """Inline the shared stylesheet and the icon into one page.

    Fails the build if a page is missing either sentinel (so it would silently
    fan a page load out into extra connections again) or if the stylesheet
    would be inlined more than once.
    """
    css_count = html.count(COMMON_CSS_LINK)
    icon_count = html.count(FAVICON_LINK)
    if css_count != 1 or icon_count != 1:
        raise SystemExit(
            f"{page_name}: expected exactly one {COMMON_CSS_LINK!r} and one "
            f"{FAVICON_LINK!r} sentinel, found {css_count} and {icon_count}. "
            "Every served page must contain both <link> tags so the build can inline them."
        )

    html = html.replace(COMMON_CSS_LINK, f'<style>{common_css}</style>')
    html = html.replace(FAVICON_LINK, f'<link rel="icon" type="image/svg+xml" href="{favicon_uri}">')

    if html.count(common_css) != 1:
        raise SystemExit(f"{page_name}: inlined stylesheet does not appear exactly once")
    if COMMON_CSS_LINK in html or FAVICON_LINK in html:
        raise SystemExit(f"{page_name}: a /common.css or /favicon.svg reference survived inlining")
    return html


def generate_header(name: str, data: bytes, original_size: int, minified_size: int) -> str:
    """Generate C++ header file with compressed data."""
    var_name = name.upper()

    # Convert bytes to C array format
    hex_values = ', '.join(f'0x{b:02x}' for b in data)

    # Split into lines of 16 bytes for readability
    hex_lines = []
    values = [f'0x{b:02x}' for b in data]
    for i in range(0, len(values), 16):
        hex_lines.append('    ' + ', '.join(values[i:i+16]))

    header = f'''// Auto-generated file - do not edit manually
// Generated by scripts/compress_web.py

#ifndef {var_name}_GZ_H
#define {var_name}_GZ_H

#include <Arduino.h>

// Original size: {original_size}, minified: {minified_size}, minified + compressed: {len(data)} bytes
const uint8_t {var_name}_GZ[] PROGMEM = {{
{',\n'.join(hex_lines)}
}};

const size_t {var_name}_GZ_LEN = {len(data)};

#endif // {var_name}_GZ_H
'''
    return header


def process_file(file_path: Path, common_css: str = "", favicon_uri: str = "") -> tuple[str, int, int]:
    """Process a single HTML, CSS or SVG file. Returns (name, original_size, compressed_size)."""
    print(f"Processing {file_path.name}...")

    # Read original content
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    original_size = len(content.encode('utf-8'))

    # Minify based on file type
    if file_path.suffix == '.css':
        minified = minify_css(content)
    elif file_path.suffix == '.svg':
        minified = minify_svg(content)
    else:
        # Inline the shared stylesheet and icon (using the same minifiers that
        # produce the standalone headers) before minifying the whole page, so
        # loading the page needs a single HTTP request.
        content = inline_page_assets(content, common_css, favicon_uri, file_path.name)
        minified = minify_html(content)
    minified_size = len(minified.encode('utf-8'))

    # Gzip compress
    compressed = gzip.compress(minified.encode('utf-8'), compresslevel=9)
    compressed_size = len(compressed)

    # Generate header file
    name = file_path.stem.replace('-', '_').replace('.', '_')
    header = generate_header(name, compressed, original_size, minified_size)

    # Write header file
    header_path = GENERATED_DIR / f"{name}_gz.h"
    with open(header_path, 'w', encoding='utf-8') as f:
        f.write(header)

    print(f"  Original: {original_size:,} bytes")
    print(f"  Minified: {minified_size:,} bytes ({100 - minified_size * 100 // original_size}% reduction)")
    print(f"  Gzipped:  {compressed_size:,} bytes ({100 - compressed_size * 100 // original_size}% total reduction)")
    print(f"  Output:   {header_path}")

    return name, original_size, compressed_size


def main():
    """Main entry point."""
    print("=" * 60)
    print("Web Content Compression")
    print("=" * 60)

    # Create output directory
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)

    # Check if data directory exists
    if not DATA_DIR.exists():
        print(f"Error: Data directory not found: {DATA_DIR}")
        print("Please create web files in the data/ directory.")
        sys.exit(1)

    # Shared assets inlined into every page (kept as separate sources so the
    # stylesheet/icon are not copy-pasted into each HTML file).
    common_css_path = DATA_DIR / "common.css"
    favicon_path = DATA_DIR / "favicon.svg"
    if not common_css_path.exists() or not favicon_path.exists():
        print(f"Error: {common_css_path} and {favicon_path} are required for inlining.")
        sys.exit(1)
    common_css = minify_css(common_css_path.read_text(encoding='utf-8'))
    favicon_uri = svg_data_uri(minify_svg(favicon_path.read_text(encoding='utf-8')))

    # Process all HTML, CSS and SVG files
    web_files = list(DATA_DIR.glob("*.html")) + list(DATA_DIR.glob("*.css")) + list(DATA_DIR.glob("*.svg"))
    if not web_files:
        print(f"Warning: No HTML/CSS files found in {DATA_DIR}")
        sys.exit(0)

    total_original = 0
    total_compressed = 0

    for file_path in sorted(web_files):
        name, original, compressed = process_file(file_path, common_css, favicon_uri)
        total_original += original
        total_compressed += compressed
        print()

    print("=" * 60)
    print(f"Total: {total_original:,} -> {total_compressed:,} bytes ({100 - total_compressed * 100 // total_original}% reduction)")
    print("=" * 60)


# Run when called directly or as PlatformIO script
if __name__ == "__main__" or PLATFORMIO_BUILD:
    main()
