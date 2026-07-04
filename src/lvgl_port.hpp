// LVGL <-> Presto display/touch glue.
#pragma once

#include "lvgl.h"
#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"

// Initialise LVGL with the Presto display (already init()ed on core1) and the
// FT6236 touch controller. front_gfx wraps the front buffer that
// presto->update() copies to the scanout back buffer.
void lvgl_port_init(pimoroni::ST7701* presto, pimoroni::PicoGraphics_PenRGB565* front_gfx);

// Latest touch state (240x240 coordinate space), for UI display/debug.
bool lvgl_port_touch_state(uint16_t* x, uint16_t* y);
