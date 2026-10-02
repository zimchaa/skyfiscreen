// LVGL display + touch port for the Pimoroni Presto.
//
// Display path
// ------------
// LVGL renders dirty areas into two small stripe buffers (RGB565). The flush
// callback byte-swaps each stripe (PicoGraphics/the ST7701 store RGB565
// big-endian) and copies it into the front buffer; on the last flush of a
// frame, presto->update() copies front -> scanout back buffer while racing
// the beam position, giving tear-free updates without waiting for vsync.
// The scanout itself (PIO + DMA + ISRs) runs on core1 — see main.cpp.
//
// Touch
// -----
// FT6236 on I2C1 (SDA 30, SCL 31, INT 32, addr 0x48), polled from the LVGL
// indev callback. Protocol ported from the MicroPython driver in
// pimoroni/presto (modules/py_frozen/touch.py): write register 0x00, read
// 15 bytes; per-touch record at offset 3+n*6 is
//   [event|x_hi, x_lo, id|y_hi, y_lo, ...]
// with coordinates in 480-space (scaled here to the logical resolution).
// The INT line is held low while a touch is active, so we only touch the
// I2C bus when there is something to read (or a release to notice).

#include "lvgl_port.hpp"
#include "display_config.hpp"

#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <cstring>

using namespace pimoroni;

static ST7701* s_presto = nullptr;
static PicoGraphics_PenRGB565* s_gfx = nullptr;
static uint16_t* s_front = nullptr;
static uint16_t s_hres = 0, s_vres = 0;
static uint32_t s_frames = 0;

// Two stripe draw buffers so LVGL can render the next stripe while the
// previous one is being copied (sizing in display_config.hpp).
static lv_color16_t stripe_a[DISPLAY_WIDTH * LVGL_STRIPE_LINES];
static lv_color16_t stripe_b[DISPLAY_WIDTH * LVGL_STRIPE_LINES];

#if PRESTO_FULL_RES
// LVGL's heap pool (widgets/styles/draw tasks) sits in PSRAM in full-res
// builds — SRAM is consumed by the scanout buffer. Hooked in via
// LV_MEM_POOL_ALLOC in lv_conf.h.
#include "lvgl_psram_pool.h"
#include "pico/platform/sections.h"
static uint8_t __uninitialized_psram("lvgl_pool") lv_pool[LV_MEM_SIZE];
extern "C" void* lvgl_psram_pool(size_t size) {
    LV_ASSERT(size <= sizeof(lv_pool));
    return lv_pool;
}
#endif

// ── Display ──────────────────────────────────────────────────────────

static void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);

    // LVGL renders native RGB565; the Presto framebuffer wants it byte-swapped.
    lv_draw_sw_rgb565_swap(px_map, (uint32_t)(w * h));

    const uint16_t* src = (const uint16_t*)px_map;
    for (int32_t row = 0; row < h; row++) {
        uint16_t* dst = s_front + (area->y1 + row) * s_hres + area->x1;
        memcpy(dst, src, (size_t)w * sizeof(uint16_t));
        src += w;
    }

    if (lv_display_flush_is_last(disp)) {
        s_presto->update(s_gfx);
        s_frames++;
    }
    lv_display_flush_ready(disp);
}

void lvgl_port_set_backlight(uint8_t brightness) {
    if (s_presto) s_presto->set_backlight(brightness);
}

uint32_t lvgl_port_frame_count() {
    return s_frames;
}

static uint32_t tick_cb(void) {
    return to_ms_since_boot(get_absolute_time());
}

// ── Touch (FT6236) ───────────────────────────────────────────────────

static const uint TOUCH_SDA = 30;
static const uint TOUCH_SCL = 31;
static const uint TOUCH_INT = 32;
static const uint8_t TOUCH_ADDR = 0x48;

static bool s_touch_down = false;
static uint16_t s_touch_x = 0, s_touch_y = 0;

static void touch_init() {
    i2c_init(i2c1, 400 * 1000);
    gpio_set_function(TOUCH_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TOUCH_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TOUCH_SDA);
    gpio_pull_up(TOUCH_SCL);

    gpio_init(TOUCH_INT);
    gpio_set_dir(TOUCH_INT, GPIO_IN);
    gpio_pull_up(TOUCH_INT);   // FT6236 pulls INT low while a touch is active
}

static void touch_poll() {
    if (gpio_get(TOUCH_INT) && !s_touch_down) return;   // nothing new

    uint8_t reg = 0x00;
    uint8_t buf[15];
    if (i2c_write_timeout_us(i2c1, TOUCH_ADDR, &reg, 1, true, 1000) != 1 ||
        i2c_read_timeout_us(i2c1, TOUCH_ADDR, buf, sizeof(buf), false, 2000) != (int)sizeof(buf)) {
        s_touch_down = false;
        return;
    }

    s_touch_down = false;
    uint8_t touches = buf[2] & 0x0f;
    for (uint8_t n = 0; n < touches && n < 2; n++) {
        const uint8_t* d = &buf[3 + n * 6];
        uint8_t event    = d[0] >> 6;      // 00 down, 01 up, 10 contact, 11 none
        uint8_t touch_id = d[2] >> 4;
        if (touch_id != 0) continue;       // first finger drives the pointer
        if (event == 0b01 || event == 0b11) continue;
        uint32_t raw_x = ((d[0] & 0x0f) << 8) | d[1];   // panel 480-space
        uint32_t raw_y = ((d[2] & 0x0f) << 8) | d[3];
        s_touch_x = (uint16_t)(raw_x * s_hres / 480);   // -> logical resolution
        s_touch_y = (uint16_t)(raw_y * s_vres / 480);
        s_touch_down = true;
    }
}

static void indev_read_cb(lv_indev_t*, lv_indev_data_t* data) {
    touch_poll();
    data->point.x = s_touch_x;
    data->point.y = s_touch_y;
    data->state = s_touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool lvgl_port_touch_state(uint16_t* x, uint16_t* y) {
    if (x) *x = s_touch_x;
    if (y) *y = s_touch_y;
    return s_touch_down;
}

// ── Init ─────────────────────────────────────────────────────────────

void lvgl_port_init(ST7701* presto, PicoGraphics_PenRGB565* front_gfx) {
    s_presto = presto;
    s_gfx = front_gfx;
    s_front = (uint16_t*)front_gfx->frame_buffer;
    s_hres = (uint16_t)front_gfx->bounds.w;
    s_vres = (uint16_t)front_gfx->bounds.h;

    lv_init();
    lv_tick_set_cb(tick_cb);

    lv_display_t* disp = lv_display_create(s_hres, s_vres);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, stripe_a, stripe_b, sizeof(stripe_a),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    touch_init();
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);
}
