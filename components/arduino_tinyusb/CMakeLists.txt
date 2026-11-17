# Vendored Arduino TinyUSB component.
#
# Arduino core 2.0.17 expects an `arduino_tinyusb` IDF component on ESP32-S2/S3
# when `CONFIG_TINYUSB_ENABLED` is set. The PlatformIO Arduino package only
# ships a precompiled `libarduino_tinyusb.a`, so the source component is kept
# here. Only the CDC class is built: `Serial` is the only USB function this
# firmware uses, and leaving MSC/HID/MIDI/DFU/vendor classes out keeps the
# component's footprint small.
#
# The component is derived from esp32-arduino-lib-builder's arduino_tinyusb
# component; TinyUSB itself is MIT-licensed (see tinyusb/LICENSE).

idf_component_register(REQUIRES esp_rom freertos soc PRIV_REQUIRES)

if(CONFIG_TINYUSB_ENABLED)
  set(compile_options
      "-DCFG_TUSB_MCU=OPT_MCU_ESP32S2"
      "-DCFG_TUSB_DEBUG=${CONFIG_TINYUSB_DEBUG_LEVEL}"
      "-Wno-type-limits" # TinyUSB core compares an untyped enum with a uint8_t
  )

  idf_component_get_property(
    FREERTOS_ORIG_INCLUDE_PATH freertos ORIG_INCLUDE_PATH
  )

  set(includes_private
      "${COMPONENT_DIR}/tinyusb/hw/bsp/"
      "${COMPONENT_DIR}/tinyusb/src/"
      "${COMPONENT_DIR}/tinyusb/src/device"
  )
  set(includes_public
      "${FREERTOS_ORIG_INCLUDE_PATH}"
      "${COMPONENT_DIR}/tinyusb/src/"
      "${COMPONENT_DIR}/include"
  )

  set(srcs
      "${COMPONENT_DIR}/src/dcd_esp32sx.c"
      "${COMPONENT_DIR}/tinyusb/src/class/cdc/cdc_device.c"
      "${COMPONENT_DIR}/tinyusb/src/common/tusb_fifo.c"
      "${COMPONENT_DIR}/tinyusb/src/device/usbd_control.c"
      "${COMPONENT_DIR}/tinyusb/src/device/usbd.c"
      "${COMPONENT_DIR}/tinyusb/src/tusb.c"
  )

  add_library(arduino_tinyusb STATIC ${srcs})
  target_include_directories(
    arduino_tinyusb
    PUBLIC ${includes_public}
    PRIVATE ${includes_private}
  )
  target_compile_options(arduino_tinyusb PRIVATE ${compile_options})
  target_link_libraries(${COMPONENT_TARGET} INTERFACE arduino_tinyusb)
endif()
