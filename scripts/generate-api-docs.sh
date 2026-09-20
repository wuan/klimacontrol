#!/bin/bash
set -e

# Install dependencies if not already installed
if ! command -v spectral &> /dev/null; then
    echo "Installing OpenAPI tools..."
    npm install -g @redocly/cli @stoplight/spectral-cli redoc-cli
fi

# Validate all OpenAPI specs
echo "Validating OpenAPI specifications..."
spectral lint docs/api/*.yaml

# Generate HTML documentation
echo "Generating HTML documentation..."
mkdir -p docs/api
redoc-cli bundle docs/api/klimacontrol-api.yaml -o docs/api/index.html

# List generated files
printf '\nGenerated documentation files:\n'
ls -lah docs/api/*.html

printf '\nAPI documentation generated successfully!\n'
