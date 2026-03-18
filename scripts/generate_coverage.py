#!/usr/bin/env python3
"""Generate LCOV coverage report from PlatformIO native test run.

This script must be run AFTER 'pio test -e native' completes.
It collects coverage data from the .gcda/.gcno files produced by the instrumented build,
filters out system and test files, and outputs coverage/coverage.info for SonarCloud.
"""

import glob
import os
import subprocess
import sys


def run_cmd(args, check=True):
    """Run a shell command, printing output. Returns returncode.

    args is always a list of fixed strings constructed internally (no user input),
    so the shell=False default is safe and appropriate here.
    """
    print(f"Running: {' '.join(args)}")
    result = subprocess.run(args, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if check and result.returncode != 0:
        print(f"Error: command failed (exit {result.returncode}): {' '.join(args)}")
        sys.exit(result.returncode)
    return result.returncode


def get_lcov_major_version():
    """Return the major version number of the installed lcov (0 if unknown)."""
    try:
        result = subprocess.run(["lcov", "--version"], capture_output=True, text=True)
        # Example output: "lcov: LCOV version 2.0-1"
        for token in result.stdout.split():
            if token and token[0].isdigit():
                try:
                    return int(token.split(".")[0])
                except ValueError:
                    pass
    except FileNotFoundError:
        pass
    return 0


def build_lcov_capture_cmd(output_file, lcov_version):
    """Return the lcov --capture command as an argument list."""
    cmd = [
        "lcov", "--capture",
        "--directory", ".pio/build/native",
        "--base-directory", ".",
        "--output-file", output_file,
    ]
    if lcov_version >= 2:
        cmd += ["--ignore-errors", "mismatch,inconsistent"]
    return cmd


def build_lcov_filter_cmd(input_file, output_file, lcov_version):
    """Return the lcov --remove filter command as an argument list."""
    cmd = [
        "lcov", "--remove", input_file,
        "/usr/*",
        "*/test/*",
        "*/.pio/*",
        "*/.platformio/*",
        "*/lib/*",
        "--output-file", output_file,
    ]
    if lcov_version >= 2:
        cmd += ["--ignore-errors", "unused"]
    return cmd


def main():
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(project_root)

    print("Checking for coverage tools...")
    if run_cmd(["lcov", "--version"], check=False) != 0:
        print("Error: 'lcov' not found. Install it with: sudo apt-get install lcov")
        sys.exit(1)

    # Locate .gcda files produced by the instrumented test binary
    gcda_files = glob.glob(".pio/build/native/**/*.gcda", recursive=True)
    if not gcda_files:
        print("Error: No .gcda files found in .pio/build/native/")
        print("Make sure 'platformio.ini' has '--coverage' in build_flags for [env:native]")
        print("and that 'pio test -e native' was executed before this script.")
        sys.exit(1)

    print(f"Found {len(gcda_files)} .gcda file(s) in .pio/build/native/")

    os.makedirs("coverage", exist_ok=True)

    lcov_version = get_lcov_major_version()

    # Capture coverage data from the native build directory
    capture_cmd = build_lcov_capture_cmd("coverage/coverage_raw.info", lcov_version)
    ret = run_cmd(capture_cmd, check=False)
    if ret != 0:
        # Retry without version-specific flags
        run_cmd(build_lcov_capture_cmd("coverage/coverage_raw.info", 0))

    # Filter out system headers, test files, PlatformIO internals, and libraries
    filter_cmd = build_lcov_filter_cmd(
        "coverage/coverage_raw.info", "coverage/coverage.info", lcov_version
    )
    ret = run_cmd(filter_cmd, check=False)
    if ret != 0:
        # Retry without version-specific flags
        run_cmd(
            build_lcov_filter_cmd(
                "coverage/coverage_raw.info", "coverage/coverage.info", 0
            )
        )

    if not os.path.exists("coverage/coverage.info"):
        print("Error: coverage/coverage.info was not created")
        sys.exit(1)

    run_cmd(["lcov", "--summary", "coverage/coverage.info"], check=False)
    print("Coverage report generated: coverage/coverage.info")


if __name__ == "__main__":
    main()
