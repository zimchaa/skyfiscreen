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

/* LVGL heap: widgets/styles/draw tasks. RP2350 has 520K SRAM; the display
 * buffers are statically allocated elsewhere. */
#define LV_MEM_SIZE (48 * 1024U)

/* 240x240 logical resolution (pixel-doubled to 480x480 by the scanout) on a
 * ~71mm active area */
#define LV_DPI_DEF 87

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

/* WiFi-join QR code panel (Phase 2) */
#define LV_USE_QRCODE 1

#endif /* LV_CONF_H */
