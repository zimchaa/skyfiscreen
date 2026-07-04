// SkyFi Screen — Phase 1: LVGL port + peripherals
//
// core1 owns the ST7701 display (init + scanout IRQs); core0 runs LVGL.
// Demo screen: a button that beeps the buzzer and pulses the ambient LEDs,
// a live touch-coordinate readout, and a backlight slider — proving display,
// touch, buzzer, RGB LEDs and backlight all work through LVGL.

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include <cstdio>

#include "lvgl.h"
#include "lvgl_port.hpp"
#include "peripherals.hpp"

using namespace pimoroni;

static const uint FRAME_WIDTH  = 240;   // half-res, pixel-doubled to 480x480
static const uint FRAME_HEIGHT = 240;

static const uint BACKLIGHT = 45;
static const uint LCD_CLK = 26;
static const uint LCD_CS  = 28;
static const uint LCD_DAT = 27;
static const uint LCD_DC  = -1;

static uint16_t back_buffer[FRAME_WIDTH * FRAME_HEIGHT];
static uint16_t front_buffer[FRAME_WIDTH * FRAME_HEIGHT];

static ST7701* g_presto = nullptr;

static void core1_entry() {
    g_presto->init();
    multicore_fifo_push_blocking(1);
    while (true) {
        tight_loop_contents();   // scanout IRQs preempt this idle loop
    }
}

// ── Phase 1 demo UI ──────────────────────────────────────────────────

static lv_obj_t* touch_label = nullptr;

static void beep_btn_event(lv_event_t* e) {
    (void)e;
    buzzer_beep(880, 120);
    leds_pulse(0, 120, 160);   // cyan flash, fades out in peripherals_task()
}

static void backlight_slider_event(lv_event_t* e) {
    lv_obj_t* slider = (lv_obj_t*)lv_event_get_target(e);
    int32_t v = lv_slider_get_value(slider);   // 10..100
    g_presto->set_backlight((uint8_t)(v * 255 / 100));
}

static void build_demo_ui() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f1a), 0);

    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "SkyFi Screen — Phase 1");
    lv_obj_set_style_text_color(title, lv_color_hex(0xe6ecff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_size(btn, 160, 70);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xe60000), 0);
    lv_obj_add_event_cb(btn, beep_btn_event, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "BEEP");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_label);

    lv_obj_t* slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 180);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 55);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, backlight_slider_event, LV_EVENT_VALUE_CHANGED, nullptr);

    touch_label = lv_label_create(scr);
    lv_label_set_text(touch_label, "touch: —");
    lv_obj_set_style_text_color(touch_label, lv_color_hex(0x8a93a6), 0);
    lv_obj_align(touch_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Refresh the coordinate readout a few times a second
    lv_timer_create([](lv_timer_t*) {
        uint16_t x, y;
        if (lvgl_port_touch_state(&x, &y)) {
            lv_label_set_text_fmt(touch_label, "touch: %u, %u", x, y);
        }
    }, 100, nullptr);
}

int main() {
    stdio_init_all();

    printf("\n=== SkyFi Screen — Phase 1 (LVGL) ===\n");
    printf("sys_clk = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));

    gpio_init(LCD_CS);
    gpio_set_dir(LCD_CS, GPIO_OUT);
    gpio_put(LCD_CS, 1);

    static ST7701 presto(FRAME_WIDTH, FRAME_HEIGHT, ROTATE_0,
                         SPIPins{spi1, LCD_CS, LCD_CLK, LCD_DAT, PIN_UNUSED, LCD_DC, BACKLIGHT},
                         back_buffer);
    static PicoGraphics_PenRGB565 gfx(FRAME_WIDTH, FRAME_HEIGHT, front_buffer);
    g_presto = &presto;

    multicore_launch_core1(core1_entry);
    multicore_fifo_pop_blocking();
    presto.set_backlight(255);

    peripherals_init();
    lvgl_port_init(&presto, &gfx);
    build_demo_ui();
    printf("LVGL up, entering main loop\n");

    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        peripherals_task();
        sleep_ms(wait_ms > 10 ? 10 : wait_ms);
    }
}
