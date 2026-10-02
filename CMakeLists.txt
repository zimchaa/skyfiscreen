cmake_minimum_required(VERSION 3.13)

# ─── Board / platform ────────────────────────────────────────────────
# Use the Presto's OWN board definition (from the pimoroni/presto repo), i.e.
# the exact environment the shipping MicroPython firmware uses: 48 GPIOs
# (RP2350B), PSRAM CS on GPIO47, and a specifically-tuned 200MHz system-clock
# PLL that the ST7701 display timing was validated against. Building on the
# generic pico_plus2 board at 240MHz produced a non-scanning (blank) panel.
set(PICO_PLATFORM rp2350-arm-s)
set(PICO_BOARD presto)
set(PICO_NUM_GPIOS 48)
# The presto board header defines the CYW43 (RM2 WiFi module) pins but, being
# MicroPython-oriented, doesn't carry the pico_board_cmake_set marker that
# makes the SDK generate the cyw43/lwIP targets — declare support ourselves.
set(PICO_CYW43_SUPPORTED 1)
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/lib/presto/boards/presto)

# ─── Local dependency paths (fully offline — no network fetch) ───────
# These are the git clones under lib/. Setting them before the import
# scripts stops CMake from trying to fetch the SDK / libraries.
set(PICO_SDK_PATH        ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
set(PIMORONI_PICO_PATH   ${CMAKE_CURRENT_LIST_DIR}/lib/pimoroni-pico)
set(PIMORONI_PRESTO_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/presto)

set(NAME skyfiscreen)

# The Presto has 8MB of QMI PSRAM on CS1 (GPIO 47). Setting this before
# pico_sdk_init() adds the PSRAM region to the linker script, so variables
# can be placed there with __in_psram / __uninitialized_psram.
set(PICO_PSRAM_SIZE_BYTES "(8 * 1024 * 1024)")

# ─── Display mode ────────────────────────────────────────────────────
# ON (default): native 480x480 — LVGL front buffer + heap in PSRAM, LVGL
#     renders into small SRAM stripes, scanout buffer in SRAM.
# OFF: 240x240 logical, pixel-doubled in hardware, everything in SRAM.
option(PRESTO_FULL_RES "Run the display at native 480x480" ON)
if (PRESTO_FULL_RES)
    add_compile_definitions(PRESTO_FULL_RES=1)   # directory-scoped: LVGL sees it too
endif()

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# ─── Presto display driver ───────────────────────────────────────────
# The st7701_presto cmake module lives under the presto repo. Add that
# repo to the include + module search paths so `include(drivers/...)`
# resolves.
set(PIMORONI_PRESTO_PATH ${PIMORONI_PRESTO_PATH} CACHE PATH "Path to the Presto libraries" FORCE)
include_directories(${PIMORONI_PRESTO_PATH})
list(APPEND CMAKE_MODULE_PATH ${PIMORONI_PRESTO_PATH})

# Pimoroni library modules (resolved via PIMORONI_PICO_PATH module path)
include(common/pimoroni_i2c)
include(common/pimoroni_bus)
include(libraries/pico_graphics/pico_graphics)
include(drivers/st7701/st7701_presto)
include(drivers/plasma/plasma)          # WS2812 ambient LEDs
include(drivers/bme280/bme280)          # Enviro Weather sensors on Qw/ST
include(drivers/ltr559/ltr559)

# ─── LVGL ────────────────────────────────────────────────────────────
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
add_subdirectory(lib/lvgl)
# For LV_MEM_POOL_INCLUDE ("lvgl_psram_pool.h") in full-res builds.
target_include_directories(lvgl PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

# ─── Application ─────────────────────────────────────────────────────
add_executable(${NAME}
    src/main.cpp
    src/lvgl_port.cpp
    src/peripherals.cpp
    src/pilink.cpp
    src/sensor_hub.cpp
    ui/ui_panel.cpp
)

# ─── Network configuration (optional; mock mode when unset) ─────────
# cmake -B build -DSKYFI_WIFI_SSID=... -DSKYFI_WIFI_PASSWORD=... \
#                -DSKYFI_API_HOST=<ip> [-DSKYFI_API_PORT=8000]
set(SKYFI_WIFI_SSID     "" CACHE STRING "WiFi SSID the panel joins")
set(SKYFI_WIFI_PASSWORD "" CACHE STRING "WiFi password")
set(SKYFI_API_HOST      "" CACHE STRING "REST API host IP (the Pi / mock server)")
set(SKYFI_API_PORT      "8000" CACHE STRING "REST API port")
if(SKYFI_WIFI_SSID AND SKYFI_API_HOST)
    target_sources(${NAME} PRIVATE src/net.cpp src/http_post.cpp)
    target_link_libraries(${NAME} pico_cyw43_arch_lwip_poll pico_lwip_http)
    add_compile_definitions(SKYFI_NET=1)          # display_config: smaller stripes
    target_compile_definitions(${NAME} PRIVATE
        SKYFI_WIFI_SSID="${SKYFI_WIFI_SSID}"
        SKYFI_WIFI_PASSWORD="${SKYFI_WIFI_PASSWORD}"
        SKYFI_API_HOST="${SKYFI_API_HOST}"
        SKYFI_API_PORT=${SKYFI_API_PORT}
    )
    message(STATUS "SkyFi net: SSID='${SKYFI_WIFI_SSID}' API=${SKYFI_API_HOST}:${SKYFI_API_PORT}")
else()
    target_sources(${NAME} PRIVATE src/net_stub.cpp)
    message(STATUS "SkyFi net: WiFi fallback not configured (USB link only)")
endif()

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/lib/lvgl
    ${CMAKE_CURRENT_LIST_DIR}/src/config      # lwipopts.h
    ${PIMORONI_PICO_PATH}/drivers/plasma
)

target_link_libraries(${NAME}
    st7701_presto
    lvgl
    plasma
    bme280
    ltr559
    pico_stdlib
    pico_multicore
    pico_rand
    pico_graphics
    hardware_interp
    hardware_i2c
    hardware_pwm
    hardware_psram
)

# With CS pin + size defined, hardware_psram's runtime_init brings the PSRAM
# up before main() (and before core1 is launched).
target_compile_definitions(${NAME} PRIVATE
    PICO_PSRAM_CS_PIN=47
    PICO_PSRAM_SIZE_BYTES=8388608
)

# ─── Outputs / stdio ─────────────────────────────────────────────────
pico_add_extra_outputs(${NAME})       # produces skyfiscreen.uf2
pico_enable_stdio_uart(${NAME} 0)
pico_enable_stdio_usb(${NAME} 1)      # printf() over USB serial for debugging
