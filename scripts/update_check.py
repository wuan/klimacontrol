import json
import os
import time

import requests

# CSRF protection: state-changing endpoints require this header
# (src/routes/RouteHelpers.h).
CSRF_HEADERS = {"X-Requested-With": "KlimaControl"}

CHECK_TIMEOUT = 60
CHECK_POLL_INTERVAL = 2
UPDATE_TIMEOUT = 900
UPDATE_POLL_INTERVAL = 5


def trigger_update(device: str) -> None:
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
        print(f"Device {device} is up to date ({check.get('current_version')})")
        return

    latest = check.get("latest_version", "unknown")
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
            # The device reboots after a successful update, so the poll
            # failing with a connection error is the expected success path.
            print(f"Update on {device} finished, device is restarting")
            return
        if response.status_code == 200:
            progress = response.json()
            status = progress.get("status")
            if status == "success":
                print(f"Update on {device} finished, device is restarting")
                return
            if status == "error":
                print(f"Error: Update failed on {device}: {progress.get('error')}")
                return
        time.sleep(UPDATE_POLL_INTERVAL)
    print(f"Error: Update on {device} timed out")


if __name__ == '__main__':
    raw_devices = os.environ['KLIMA_DEVICES']
    devices = raw_devices.split(',')
    devices = [device.strip() for device in devices]

    for device in devices:
        try:
            response = requests.get(f"http://{device}/api/status")
        except requests.exceptions.RequestException as e:
            print(f"Error: Failed to get status from {device}: {e}")
            continue
        if response.status_code != 200:
            print(f"Error: Failed to get status from {device}")
            continue
        data = response.json()
        if "firmware_version" in data:
            print(f"Device {device} has firmware version: {data['firmware_version']}")
        trigger_update(device)
