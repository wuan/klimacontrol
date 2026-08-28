#!/bin/bash
set -e

if ! command -v openspec &> /dev/null; then
    echo "Installing OpenSpec CLI..."
    if ! npm install -g @fission-ai/openspec; then
        echo "ERROR: Failed to install OpenSpec CLI"
        echo "Please install it manually with: npm install -g @fission-ai/openspec"
        exit 1
    fi
fi

# openspec/ lives at the repository root; run from there so the CLI finds it.
cd "$(dirname "$0")/.."
openspec validate --all --strict --no-interactive
