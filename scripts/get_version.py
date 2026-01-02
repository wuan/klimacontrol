#!/usr/bin/env python3
"""
Get firmware version from git tag or fallback to v0.0.0-dev
"""
Import("env")
import subprocess

def get_firmware_version():
    try:
        # Try to get the current git tag
        version = subprocess.check_output(
            ["git", "describe", "--tags", "--exact-match"],
            stderr=subprocess.DEVNULL
        ).decode('utf-8').strip()
        return version
    except:
        try:
            # Try to get the latest tag with commit count
            version = subprocess.check_output(
                ["git", "describe", "--tags", "--always"],
                stderr=subprocess.DEVNULL
            ).decode('utf-8').strip()
            return version
        except:
            # Fallback if not in git repo
            return "v0.0.0-dev"

version = get_firmware_version()
print(f"Firmware version: {version}")
env.Append(CPPDEFINES=[
    f'FIRMWARE_VERSION=\\"{version}\\"'
])
