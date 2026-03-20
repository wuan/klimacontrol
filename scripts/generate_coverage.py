#!/usr/bin/env python3
"""Generate LCOV coverage report from PlatformIO native test run.

This script must be run AFTER 'pio test -e native' completes.
It collects coverage data from the .gcda/.gcno files produced by the instrumented build,
filters out system and test files, and outputs coverage/coverage.info for SonarCloud.
It also converts the LCOV report to Sonar Generic Coverage XML format
(coverage/coverage-generic.xml) for use with sonar.coverageReportPaths.
"""

import glob
import os
import subprocess
import sys
import xml.etree.ElementTree as ET


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

    lcov_to_sonar_xml("coverage/coverage.info", "coverage/coverage-generic.xml", project_root)
    print("Sonar Generic Coverage XML generated: coverage/coverage-generic.xml")


def lcov_to_sonar_xml(lcov_file, xml_file, project_root):
    """Convert an LCOV .info file to Sonar Generic Coverage XML format.

    The Sonar Generic Coverage XML format is documented at:
    https://docs.sonarsource.com/sonarcloud/advanced-setup/test-coverage/generic-test-data/

    Source file paths in the XML are relative to the project root so that
    SonarCloud can match them to indexed files regardless of the runner's
    absolute path.
    """
    root = ET.Element("coverage", version="1")

    current_file = None
    file_elem = None

    with open(lcov_file, encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.rstrip()
            if line.startswith("SF:"):
                abs_path = line[3:]
                # Compute path relative to the project root
                try:
                    rel_path = os.path.relpath(abs_path, project_root)
                except ValueError:
                    print(
                        f"Warning: could not compute relative path for '{abs_path}' "
                        f"(project root: '{project_root}'); using absolute path",
                        file=sys.stderr,
                    )
                    rel_path = abs_path
                current_file = rel_path
                file_elem = ET.SubElement(root, "file", path=current_file)
            elif line.startswith("DA:") and file_elem is not None:
                # DA:<line number>,<hit count>
                parts = line[3:].split(",")
                if len(parts) >= 2:
                    line_number = parts[0]
                    try:
                        hit_count = int(parts[1])
                    except ValueError:
                        print(
                            f"Warning: could not parse hit count in DA entry: '{line}'",
                            file=sys.stderr,
                        )
                        continue
                    covered = "true" if hit_count > 0 else "false"
                    ET.SubElement(
                        file_elem,
                        "lineToCover",
                        lineNumber=line_number,
                        covered=covered,
                    )
            elif line == "end_of_record":
                current_file = None
                file_elem = None

    tree = ET.ElementTree(root)
    ET.indent(tree, space="  ")
    tree.write(xml_file, encoding="unicode", xml_declaration=True)


if __name__ == "__main__":
    main()
