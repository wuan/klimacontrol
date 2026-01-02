import os
import subprocess
import sys

def generate_coverage():
    print("Checking for coverage tools...")
    
    # Check if lcov is available
    try:
        subprocess.check_call(["lcov", "--version"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError, FileNotFoundError):
        print("Error: 'lcov' not found.")
        return

    print("Generating coverage report manually...")
    
    # Debug: list files
    print("Listing .gcno and .gcda files:")
    os.system("find . -name '*.gcno' -o -name '*.gcda'")
    
    # Run lcov to capture coverage data
    # Use --base-directory . to ensure paths are correct
    # Some systems require --ignore-errors graph,mismatched to proceed with partial data
    cmd = "lcov --capture --directory . --base-directory . --output-file coverage.info --ignore-errors gcov,graph"
    print(f"Executing: {cmd}")
    ret = os.system(cmd)
    if ret != 0:
        print(f"Error: lcov capture failed with return code {ret}")
        # Try without ignore-errors if it failed because of the flag
        print("Retrying without --ignore-errors...")
        cmd = "lcov --capture --directory . --base-directory . --output-file coverage.info"
        print(f"Executing: {cmd}")
        ret = os.system(cmd)
        if ret != 0:
            print(f"Error: lcov capture failed again with return code {ret}")
            return

    # Filter out system headers, tests, and .pio directory
    # Also ignore unity related files
    cmd = "lcov --remove coverage.info '/usr/*' '*/test/*' '*/.pio/*' '*/lib/*' --output-file coverage_filtered.info"
    print(f"Executing: {cmd}")
    ret = os.system(cmd)
    if ret != 0:
        print(f"Error: lcov filter failed with return code {ret}")
        return
    
    # Generate HTML report
    cmd = "genhtml coverage_filtered.info --output-directory coverage_report"
    print(f"Executing: {cmd}")
    ret = os.system(cmd)
    if ret != 0:
        print(f"Error: genhtml failed with return code {ret}")
        return
    
    print("Coverage report generated in coverage_report/index.html")

if __name__ == "__main__":
    generate_coverage()
