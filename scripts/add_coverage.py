"""
PlatformIO extra script: add --coverage to linker flags.

PlatformIO only passes build_flags to the compiler, not the linker.
This script ensures coverage instrumentation is linked correctly on
both GCC (uses -lgcov) and Clang (uses the profile runtime).
"""
Import("env")

env.Append(LINKFLAGS=["--coverage"])
