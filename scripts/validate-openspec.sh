#!/bin/bash
set -e

# Pinned to match .github/workflows/openspec.yml — a different major can
# silently validate or invalidate specs in ways CI and local disagree on.
OPENSPEC_VERSION="${OPENSPEC_VERSION:-1.10.0}"

# openspec/ lives at the repository root; run from there so the CLI finds it.
cd "$(dirname "$0")/.."

npx -y "@fission-ai/openspec@${OPENSPEC_VERSION}" validate --all --strict --no-interactive
