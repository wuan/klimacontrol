cmake_minimum_required(VERSION 3.16)

# The Arduino core is built as an IDF component, so the board defines the
# precompiled Arduino framework used to inject through `build.extra_flags`
# must be visible to that component's own compilation. PlatformIO already
# applies the USB CDC flag to the project sources via `build_flags`; these
# definitions make the Arduino core agree on the `Serial` type and pin map.
add_compile_definitions(
    ARDUINO_USB_CDC_ON_BOOT=1
    ARDUINO_USB_MODE=0
    BOARD_HAS_PSRAM
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(klimacontrol)
