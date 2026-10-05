import json
import os
import re
import sys
import time

import requests

# CSRF protection: state-changing endpoints require this header
# (src/routes/RouteHelpers.h).
CSRF_HEADERS = {"X-Requested-With": "KlimaControl"}

# Must match OTA_GITHUB_OWNER / OTA_GITHUB_REPO (src/ota/OTAConfig.h) — the
# device flashes only what its own check of the compiled-in owner/repo found.
GITHUB_OWNER = "wuan"
GITHUB_REPO = "klimacontrol"

CHECK_TIMEOUT = 60
CHECK_POLL_INTERVAL = 2
UPDATE_TIMEOUT = 900
UPDATE_POLL_INTERVAL = 5
RESTART_TIMEOUT = 180
RESTART_POLL_INTERVAL = 3


def parse_version(version: str):
    """Parse the vMAJOR.MINOR.PATCH prefix of a version tag. Any trailing
    suffix is ignored — a git-describe dev build ("v1.2.3-4-gabc1234") parses
    as equal to the v1.2.3 release, mirroring Support::compareVersions
    (src/ota/VersionCompare.h). Returns None when the version does not parse."""
    match = re.match(r"v(\d+)\.(\d+)\.(\d+)", version or "")
    if not match:
        return None
    return tuple(int(part) for part in match.groups())


def fetch_latest_release():
    """Look up the latest release tag on GitHub once, so the per-device
    check only runs on devices that are actually outdated."""
    try:
        response = requests.get(
            f"https://api.github.com/repos/{GITHUB_OWNER}/{GITHUB_REPO}/releases/latest",
            timeout=10,
        )
    except requests.exceptions.RequestException as e:
        print(f"Error: Failed to look up the latest release on GitHub: {e}")
        return None
    if response.status_code != 200:
        print(f"Error: Failed to look up the latest release on GitHub: HTTP {response.status_code}")
        return None
    tag = response.json().get("tag_name")
    if not tag or parse_version(tag) is None:
        print(f"Error: Latest release tag {tag!r} is not a vMAJOR.MINOR.PATCH version")
        return None
    return tag


def running_version(device: str):
    """Return the firmware version reported by /api/status, or None if the
    device does not answer."""
    try:
        response = requests.get(f"http://{device}/api/status", timeout=10)
    except requests.exceptions.RequestException:
        return None
    if response.status_code != 200:
        return None
    return response.json().get("firmware_version")


def wait_for_device(device: str):
    """Poll until the device answers /api/status again after a connection
    loss. Returns the reported firmware version, or None once
    RESTART_TIMEOUT has elapsed."""
    deadline = time.monotonic() + RESTART_TIMEOUT
    while time.monotonic() < deadline:
        version = running_version(device)
        if version is not None:
            return version
        time.sleep(RESTART_POLL_INTERVAL)
    return None


def verify_rebooted_version(device: str, expected_version: str, rebooted: bool) -> None:
    """Wait for the post-update reboot and confirm the expected firmware is
    running.

    With rebooted=False the device has only *reported* success and may still
    serve the old firmware for a few seconds — an old-version answer is not
    a failure until the device has been seen offline at least once. With
    rebooted=True the reboot is already known to have happened (progress
    state reset to "idle", or the device went down and returned), so the
    next /api/status answer is final.
    """
    deadline = time.monotonic() + RESTART_TIMEOUT
    went_down = rebooted
    while time.monotonic() < deadline:
        try:
            response = requests.get(f"http://{device}/api/status", timeout=10)
        except requests.exceptions.RequestException:
            went_down = True
            time.sleep(RESTART_POLL_INTERVAL)
            continue
        if response.status_code == 200:
            version = response.json().get("firmware_version")
            if version == expected_version:
                print(f"Device {device} is back online with firmware {version}")
                return
            if went_down:
                print(
                    f"Error: Device {device} restarted with firmware {version}, "
                    f"expected {expected_version} (update did not stick)"
                )
                return
        time.sleep(RESTART_POLL_INTERVAL)
    print(
        f"Error: Device {device} did not come back online with firmware "
        f"{expected_version} within {RESTART_TIMEOUT}s"
    )


def trigger_update(device: str, latest: str) -> None:
    # The device still runs its own check: POST /api/ota/update refuses to
    # start unless a check has produced a verified result on the device
    # (OTAUpdater::startBackgroundUpdateFromLatestCheck). The script-side
    # lookup above only decides *whether* it is worth arming the device.
    try:
        response = requests.post(
            f"http://{device}/api/ota/check",
            headers=CSRF_HEADERS,
            timeout=10,
        )
    except requests.exceptions.RequestException as e:
        print(f"Error: Failed to start update check on {device}: {e}")
        return
    if response.status_code == 409:
        print(f"Device {device} is busy, skipping update check")
        return
    if response.status_code != 202:
        print(f"Error: Failed to start update check on {device}: HTTP {response.status_code}")
        return

    # GET /api/ota/check answers 200 with status "checking" while the
    # background check is still running; only leave the loop on a terminal
    # status ("done" / "error").
    deadline = time.monotonic() + CHECK_TIMEOUT
    while True:
        try:
            response = requests.get(f"http://{device}/api/ota/check", timeout=10)
        except requests.exceptions.RequestException as e:
            print(f"Error: Failed to poll update check on {device}: {e}")
            return
        if response.status_code == 200 and response.json().get("status") != "checking":
            check = response.json()
            break
        if time.monotonic() >= deadline:
            print(f"Error: Update check on {device} timed out")
            return
        time.sleep(CHECK_POLL_INTERVAL)

    if check.get("status") == "error":
        print(f"Error: Update check failed on {device}: {check.get('error')}")
        return

    if not check.get("update_available"):
        print(f"Device {device} reported no installable update ({check.get('current_version')})")
        return

    print(f"Device {device}: {check.get('current_version')} -> {latest}, starting update")

    # The update must be triggered with a JSON body: a bodyless POST hits
    # HTTP 501 on affected firmware (see
    # https://github.com/wuan/klimacontrol/issues/44). Only
    # {"allow_reinstall": false} is sent — the device installs strictly
    # newer releases only.
    try:
        response = requests.post(
            f"http://{device}/api/ota/update",
            headers={**CSRF_HEADERS, "Content-Type": "application/json"},
            data=json.dumps({"allow_reinstall": False}),
            timeout=10,
        )
    except requests.exceptions.RequestException as e:
        print(f"Error: Failed to start update on {device}: {e}")
        return
    if response.status_code == 409:
        print(f"Error: Device {device} refused to start the update (busy)")
        return
    if response.status_code != 200:
        print(f"Error: Device {device} refused to start the update: HTTP {response.status_code}")
        return

    deadline = time.monotonic() + UPDATE_TIMEOUT
    while time.monotonic() < deadline:
        try:
            response = requests.get(f"http://{device}/api/ota/update", timeout=10)
        except requests.exceptions.RequestException:
            # The device drops off the network when it reboots after flashing
            # — but the same symptom can be a transient WiFi hiccup while the
            # download is still running. Wait for the device to answer again
            # and re-read the progress state instead of assuming success: if
            # it rebooted, the state has reset to "idle" and the idle branch
            # below decides.
            if wait_for_device(device) is None:
                print(
                    f"Error: Device {device} did not come back online within "
                    f"{RESTART_TIMEOUT}s of the connection loss"
                )
                return
            continue
        if response.status_code == 200:
            progress = response.json()
            status = progress.get("status")
            if status == "success":
                print(f"Update on {device} finished, device is restarting")
                verify_rebooted_version(device, latest, rebooted=False)
                return
            if status == "error":
                print(f"Error: Update failed on {device}: {progress.get('error')}")
                return
            if status == "idle":
                # The progress state lives in static memory and is set to
                # "downloading" before POST /api/ota/update even returns, so
                # "idle" here can only mean the device rebooted and a poll
                # missed the brief "success" window between two polls.
                print(f"Device {device} rebooted during the update, verifying firmware version")
                verify_rebooted_version(device, latest, rebooted=True)
                return
        time.sleep(UPDATE_POLL_INTERVAL)
    print(f"Error: Update on {device} timed out")


if __name__ == '__main__':
    latest = fetch_latest_release()
    if latest is None:
        sys.exit(1)
    latest_parsed = parse_version(latest)
    print(f"Latest release: {latest}")

    raw_devices = os.environ['KLIMA_DEVICES']
    devices = raw_devices.split(',')
    devices = [device.strip() for device in devices]

    for device in devices:
        current = running_version(device)
        if current is None:
            print(f"Error: Failed to get status from {device}")
            continue
        print(f"Device {device} has firmware version: {current}")

        current_parsed = parse_version(current)
        if current_parsed is None:
            print(f"Error: Firmware version {current!r} on {device} does not parse, skipping update")
            continue
        # Strictly newer only, like the device itself: a dev build
        # ("v1.2.3-4-gabc1234") is never handed its own release as an update.
        if current_parsed >= latest_parsed:
            print(f"Device {device} is up to date ({latest} is the latest release)")
            continue

        trigger_update(device, latest)
