# Code Coverage for Klima-Control

This document explains how to generate and analyze code coverage for the Klima-Control project using SonarCloud.

## Overview

The project includes comprehensive code coverage support for SonarCloud analysis. Coverage is generated using PlatformIO's native testing environment and can be analyzed both locally and through CI/CD pipelines.

## Local Coverage Generation

### Prerequisites

- PlatformIO installed (`pip install platformio`)
- Python 3.11+
- Bash shell

### Quick Start

```bash
# Generate a basic coverage report
./scripts/generate_coverage.sh

# Generate a comprehensive coverage report with file analysis
./scripts/generate_real_coverage.sh
```

### Coverage Scripts

1. **`generate_coverage.sh`** - Basic coverage report for quick testing
2. **`generate_real_coverage.sh`** - Comprehensive report analyzing all source files

Both scripts:
- Run all native tests
- Generate coverage data in LCOV format
- Create HTML reports
- Output coverage summaries

## SonarCloud Integration

### GitHub Actions Workflow

The project includes SonarCloud integration in the existing test workflow:

**File:** `.github/workflows/test.yml`

**Triggers:**
- Pushes to `main` branch
- Pull requests to `main` branch

The workflow:
1. Runs native tests with coverage instrumentation
2. Generates coverage reports using `scripts/generate_coverage.sh`
3. Performs SonarQube analysis with coverage data
4. Builds firmware and checks size constraints

### Setup Instructions

1. **Create a SonarQube/SonarCloud account** 
   - For SonarCloud: [https://sonarcloud.io](https://sonarcloud.io)
   - For self-hosted SonarQube: Set up your server

2. **Set up the project:**
   - Create a new organization (or use existing)
   - Create a new project with key `wuan_klimacontrol`

3. **Add GitHub secrets:**
   ```bash
   # Go to your GitHub repository Settings > Secrets > Actions
   # Add a new repository secret:
   Name: SONAR_TOKEN
   Value: <your-sonar-token>
   ```

4. **Push to trigger analysis:**
   ```bash
   git push origin main
   ```

### Configuration Files

- **`.github/workflows/test.yml`** - GitHub Actions workflow with SonarQube integration
- **`sonar-project.properties`** - SonarQube/SonarCloud configuration
- **`scripts/generate_coverage.sh`** - Robust coverage generation script

## Coverage Configuration

### Exclusions

The following files/directories are excluded from coverage analysis:

- `test/` - Test files
- `scripts/` - Build scripts
- `src/generated/` - Auto-generated code
- Hardware-specific files (`**/hardware/`, `**/esp32/`, `**/arduino/`, `**/platformio/`)
- `main.cpp` - Entry point

### SonarCloud Properties

Key configuration in `sonar-project.properties`:

```properties
# Project identification
sonar.projectKey=wuan_klimacontrol
sonar.organization=wuan

# Source files
sonar.sources=src
sonar.exclusions=test/**,**/test_**,**/*_test.cpp,scripts/**,src/generated/**

# Coverage
sonar.cfamily.coverage.reportPaths=coverage/coverage.info
sonar.coverage.exclusions=**/hardware/**,**/esp32/**,**/arduino/**,**/platformio/**,**/main.cpp

# Tests
sonar.test.inclusions=test/**
sonar.tests=test
```

## PlatformIO Configuration

The native testing environment is configured in `platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -O0 -g
build_src_filter = +<Config.cpp> +<support/Stats.cpp>
test_build_src = yes
build_type = debug
build_unflags = -Os
```

## Viewing Coverage Reports

### Local HTML Reports

After running coverage scripts, open:
```bash
open coverage/html/index.html
```

### SonarCloud Dashboard

After successful CI run, view results at:
```
https://sonarcloud.io/dashboard?id=wuan_klimacontrol
```

## Troubleshooting

### macOS gcov Issues

If you encounter gcov linking issues on macOS:

1. **Use the simplified coverage script:**
   ```bash
   ./scripts/generate_coverage.sh
   ```

2. **For full coverage, use Docker or Linux:**
   ```bash
   docker run -v $(pwd):/workspace -w /workspace ubuntu:latest \
     bash -c "apt update && apt install -y lcov gcovr && ./scripts/generate_real_coverage.sh"
   ```

### Common Issues

**"Library gcov not found"**: Use the simplified coverage script or run on Linux

**"No coverage data"**: Ensure tests are passing and source files are included

**"SonarCloud token invalid"**: Regenerate token and update GitHub secrets

## Advanced Usage

### Custom Coverage Analysis

To analyze specific files or adjust coverage percentages, modify the coverage generation scripts.

### CI/CD Integration

For custom CI pipelines, use the GitHub Actions workflow as a template and adapt to your CI system.

### Quality Gates

Configure quality gates in SonarCloud to enforce coverage requirements:
- Minimum line coverage (e.g., 80%)
- Minimum function coverage (e.g., 70%)
- Technical debt thresholds

## Best Practices

1. **Run coverage locally before pushing** to catch issues early
2. **Review SonarCloud results** after each PR
3. **Add tests for new features** to maintain coverage
4. **Update exclusions** when adding hardware-specific code
5. **Monitor technical debt** and address issues regularly

## Limitations

- **macOS gcov support**: Limited due to Apple's toolchain differences
- **Hardware-specific code**: Excluded from coverage (as expected)
- **Template-heavy code**: May show lower coverage due to instantiation patterns

For production use, run coverage analysis on Linux environments for best results.
