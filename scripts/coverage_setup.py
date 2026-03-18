Import("env")

# Coverage setup script for PlatformIO
print("Setting up coverage instrumentation...")

# Ensure coverage flags are set for both C and C++
env.Append(
    CCFLAGS=["-fprofile-arcs", "-ftest-coverage", "--coverage"],
    CXXFLAGS=["-fprofile-arcs", "-ftest-coverage", "--coverage"],
    LINKFLAGS=["--coverage"]
)

# Debug output
print("Coverage flags configured:")
print("  CCFLAGS:", env.get("CCFLAGS", []))
print("  CXXFLAGS:", env.get("CXXFLAGS", []))
print("  LINKFLAGS:", env.get("LINKFLAGS", []))
