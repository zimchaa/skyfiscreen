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
list(APPEND PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/lib/presto/boards/presto)

# ─── Local dependency paths (fully offline — no network fetch) ───────
# These are the git clones under lib/. Setting them before the import
# scripts stops CMake from trying to fetch the SDK / libraries.
set(PICO_SDK_PATH        ${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk)
set(PIMORONI_PICO_PATH   ${CMAKE_CURRENT_LIST_DIR}/lib/pimoroni-pico)
set(PIMORONI_PRESTO_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/presto)

set(NAME skyfiscreen)

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
include(common/pimoroni_bus)
include(libraries/pico_graphics/pico_graphics)
include(drivers/st7701/st7701_presto)
include(drivers/plasma/plasma)          # WS2812 ambient LEDs

# ─── LVGL ────────────────────────────────────────────────────────────
set(LV_CONF_PATH ${CMAKE_CURRENT_LIST_DIR}/lv_conf.h)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
add_subdirectory(lib/lvgl)

# ─── Application ─────────────────────────────────────────────────────
add_executable(${NAME}
    src/main.cpp
    src/lvgl_port.cpp
    src/peripherals.cpp
    src/mock_data.cpp
    ui/ui_panel.cpp
)

target_include_directories(${NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/lib/lvgl
    ${PIMORONI_PICO_PATH}/drivers/plasma
)

target_link_libraries(${NAME}
    st7701_presto
    lvgl
    plasma
    pico_stdlib
    pico_multicore
    pico_rand
    pico_graphics
    hardware_interp
    hardware_i2c
    hardware_pwm
)

# ─── Outputs / stdio ─────────────────────────────────────────────────
pico_add_extra_outputs(${NAME})       # produces skyfiscreen.uf2
pico_enable_stdio_uart(${NAME} 0)
pico_enable_stdio_usb(${NAME} 1)      # printf() over USB serial for debugging
