/**
 * lv_conf.h — LVGL configuration for SkyFi Screen (Pimoroni Presto, RP2350)
 *
 * Only deviations from LVGL defaults are set here; everything else falls
 * back to the defaults in lv_conf_internal.h.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* Presto framebuffer is RGB565 (byte-swapped at flush time for the ST7701) */
#define LV_COLOR_DEPTH 16

#if PRESTO_FULL_RES

/* Native 480x480 (see src/display_config.hpp): the scanout buffer eats
 * nearly all of SRAM, so LVGL's heap lives in PSRAM (pool provided by
 * lvgl_port.cpp). Charts + tabs need room; PSRAM is 8MB. */
#define LV_MEM_SIZE (256 * 1024U)
#define LV_MEM_POOL_INCLUDE "lvgl_psram_pool.h"
#define LV_MEM_POOL_ALLOC lvgl_psram_pool
#define LV_DPI_DEF 174

#else

/* 240x240 logical resolution, hardware pixel-doubled to the 480x480 panel */
#define LV_MEM_SIZE (48 * 1024U)
#define LV_DPI_DEF 87

#endif /* PRESTO_FULL_RES */

/* We drive lv_timer_handler() from the main loop and feed ticks via
 * lv_tick_set_cb() */
#define LV_DEF_REFR_PERIOD 16

/* Logging: keep warnings visible over USB serial during development */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fonts: default 14 plus larger sizes for tiles and the LAND NOW button */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_40 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1

/* WiFi-join QR code panel (Phase 2) */
#define LV_USE_QRCODE 1

#endif /* LV_CONF_H */
