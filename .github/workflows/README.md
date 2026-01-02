# GitHub Actions Workflows

This directory contains automated workflows for building, testing, and releasing firmware.

## Workflows

### 1. PlatformIO CI - Build and Test (`test.yml`)

**Triggers:**
- Push to `main` branch
- Pull requests to `main`

**What it does:**
- ✅ Runs all native tests (test_color, test_jump, test_smooth_blend)
- 🔨 Builds firmware for ESP32-S3
- 📊 Checks firmware size and partition constraints (1856KB max)
- 📦 Uploads build artifacts (7-day retention)

**Status badge:**
```markdown
[![PlatformIO CI](https://github.com/YOUR_USERNAME/untitled/actions/workflows/test.yml/badge.svg)](https://github.com/YOUR_USERNAME/untitled/actions/workflows/test.yml)
```

### 2. Build and Release Firmware (`release.yml`)

**Triggers:**
- Push version tags (e.g., `v1.0.0`, `v1.2.3-beta`)

**What it does:**
- 🔨 Builds firmware for ESP32-S3
- 📝 Generates release notes with installation instructions
- 🚀 Creates GitHub Release with firmware.bin
- 📦 Uploads firmware artifacts (90-day retention)
- 🏷️ Marks pre-releases for beta/alpha/rc tags

**Status badge:**
```markdown
[![Release](https://github.com/YOUR_USERNAME/untitled/actions/workflows/release.yml/badge.svg)](https://github.com/YOUR_USERNAME/untitled/actions/workflows/release.yml)
```

## Creating a Release

### Method 1: Using Git Tags (Automated)

```bash
# 1. Update version in OTAConfig.h
vim src/OTAConfig.h
# Change: #define FIRMWARE_VERSION "v1.0.1"

# 2. Commit the version change
git add src/OTAConfig.h
git commit -m "bump version to v1.0.1"

# 3. Create and push tag
git tag v1.0.1
git push origin v1.0.1

# 4. GitHub Actions automatically:
#    - Builds firmware
#    - Creates release
#    - Uploads firmware.bin
```

**Done!** Your device can now update via OTA.

### Method 2: Using GitHub CLI

```bash
# Same as above, but create tag with gh
gh release create v1.0.1 \
  --title "Version 1.0.1" \
  --notes "Bug fixes and improvements" \
  --generate-notes

# GitHub Actions still builds and attaches firmware
```

### Method 3: Manual Release

If you prefer to build locally and skip CI:

```bash
# Build locally
pio run -e adafruit_qtpy_esp32s3_nopsram

# Create release with gh CLI
gh release create v1.0.1 \
  .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin \
  --title "Version 1.0.1" \
  --notes "Bug fixes and improvements"
```

## Version Numbering

Use [Semantic Versioning](https://semver.org/):

```
v1.2.3
  │ │ │
  │ │ └─ PATCH: Bug fixes (v1.0.1)
  │ └─── MINOR: New features (v1.1.0)
  └───── MAJOR: Breaking changes (v2.0.0)
```

**Special tags:**
- `v1.0.0-beta.1` - Beta release (marked as pre-release)
- `v1.0.0-alpha.2` - Alpha release (marked as pre-release)
- `v1.0.0-rc.1` - Release candidate (marked as pre-release)

## Viewing Workflow Runs

1. Go to your repository on GitHub
2. Click **"Actions"** tab
3. Select a workflow from the left sidebar
4. Click on a run to see details

## Debugging Failed Workflows

### Build Fails

```bash
# Test locally first
pio run -e adafruit_qtpy_esp32s3_nopsram

# Check firmware size
ls -lh .pio/build/adafruit_qtpy_esp32s3_nopsram/firmware.bin
```

### Tests Fail

```bash
# Run tests locally
pio test -e native -v
```

### Release Not Created

- Check that tag starts with `v` (v1.0.0, not 1.0.0)
- Verify workflow has permissions to create releases
- Check Actions logs for errors

## Workflow Permissions

If releases aren't being created, check repository settings:

1. Go to **Settings** > **Actions** > **General**
2. Under "Workflow permissions", select:
   - ✅ **Read and write permissions**
   - ✅ **Allow GitHub Actions to create and approve pull requests**

## Customization

### Change Build Environment

Edit `.github/workflows/release.yml`:

```yaml
- name: Build Firmware
  run: |
    pio run -e your_custom_environment
```

### Add Build Flags

Edit `platformio.ini`:

```ini
[env:adafruit_qtpy_esp32s3_nopsram]
build_flags =
  -DFIRMWARE_VERSION=\"${GITHUB_REF_NAME}\"
  -DGIT_COMMIT=\"${GITHUB_SHA}\"
```

### Change Artifact Retention

Edit retention days in workflows:

```yaml
- name: Upload Build Artifacts
  uses: actions/upload-artifact@v4
  with:
    retention-days: 30  # Change from 90
```

## Caching

Workflows use caching to speed up builds:

- **Python packages**: `pip cache`
- **PlatformIO packages**: `~/.platformio`
- **Build artifacts**: `.pio`

Cache is invalidated when `platformio.ini` changes.

## Manual Workflow Trigger

You can also trigger workflows manually:

1. Go to **Actions** tab
2. Select workflow
3. Click **"Run workflow"** button
4. Choose branch and options

## Status Checks

Add to your `README.md`:

```markdown
[![PlatformIO CI](https://github.com/YOUR_USERNAME/untitled/actions/workflows/test.yml/badge.svg)](https://github.com/YOUR_USERNAME/untitled/actions/workflows/test.yml)
[![Release](https://github.com/YOUR_USERNAME/untitled/actions/workflows/release.yml/badge.svg)](https://github.com/YOUR_USERNAME/untitled/actions/workflows/release.yml)
```

## Next Steps

1. Replace `YOUR_USERNAME` in badge URLs above
2. Push workflows to GitHub: `git push origin ota_setup`
3. Create your first release: `git tag v1.0.0 && git push origin v1.0.0`
4. Watch the magic happen! ✨
