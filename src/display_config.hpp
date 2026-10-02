// Display mode configuration, shared by main.cpp and lvgl_port.cpp.
//
// Default: 240x240 logical resolution; the ST7701 driver pixel-doubles to
// the physical 480x480 panel, and the whole display stack lives in SRAM.
//
// Build with PRESTO_FULL_RES=1 (cmake -DPRESTO_FULL_RES=ON) for native
// 480x480: sharper text and images, at the cost of 4x the pixels to render
// and a front buffer that has to live in PSRAM (a 450KB RGB565 buffer no
// longer fits in SRAM beside the 450KB scanout buffer).
#pragma once

#include <cstdint>

#if PRESTO_FULL_RES

inline constexpr uint16_t DISPLAY_WIDTH  = 480;
inline constexpr uint16_t DISPLAY_HEIGHT = 480;
// SRAM is nearly exhausted by the 450KB scanout buffer, so the LVGL stripe
// buffers shrink: 480x20 px x 2 bytes = 18.75KB each. This is close to the
// limit — at 24 lines the link fails (heap overlaps the core1 stack).
// Builds with the WiFi fallback also carry the CYW43 driver's static state
// in SRAM, so they drop to 16 lines to keep a few KB of malloc heap free.
#if SKYFI_NET
inline constexpr uint32_t LVGL_STRIPE_LINES = 16;
#else
inline constexpr uint32_t LVGL_STRIPE_LINES = 20;
#endif

#else

inline constexpr uint16_t DISPLAY_WIDTH  = 240;
inline constexpr uint16_t DISPLAY_HEIGHT = 240;
// 240x60 px x 2 bytes = 28.8KB each.
inline constexpr uint32_t LVGL_STRIPE_LINES = 60;

#endif
