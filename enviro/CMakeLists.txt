cmake_minimum_required(VERSION 3.13)

# Enviro hub firmware for the Pimoroni Enviro Weather (RP2040 / Pico W).
# Separate build from the Presto firmware; shares lib/pico-sdk and the
# register map in ../common/enviro_hub_regs.h.
set(PICO_BOARD pico_w)
set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/../lib/pico-sdk)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(enviro_hub C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_executable(enviro_hub enviro_hub.c)
target_include_directories(enviro_hub PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../common)
target_link_libraries(enviro_hub pico_stdlib pico_i2c_slave hardware_i2c hardware_adc hardware_watchdog)

pico_add_extra_outputs(enviro_hub)
pico_enable_stdio_usb(enviro_hub 1)   # debug log + picotool reflash over USB
pico_enable_stdio_uart(enviro_hub 0)
