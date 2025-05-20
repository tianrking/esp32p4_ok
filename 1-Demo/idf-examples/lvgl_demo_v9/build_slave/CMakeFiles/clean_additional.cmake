# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader/bootloader.bin"
  "bootloader/bootloader.elf"
  "bootloader/bootloader.map"
  "config/sdkconfig.cmake"
  "config/sdkconfig.h"
  "esp-idf/esptool_py/flasher_args.json.in"
  "esp-idf/mbedtls/x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "network_adapter.bin"
  "network_adapter.map"
  "project_elf_src_esp32c6.c"
  "x509_crt_bundle.S"
  "/home/w0x7ce/Downloads/esp32_p4/JC8012P4A1C_I_W_Y/1-Demo/idf-examples/lvgl_demo_v9/managed_components/espressif__esp_hosted/slave/main/coprocessor_fw_version.h"
  )
endif()
