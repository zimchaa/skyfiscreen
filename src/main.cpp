// SkyFi Screen — Phase 0 (core1 display architecture)
//
// The ST7701 driver scans the panel out using PIO + DMA + two PIO IRQs that
// must be *serviced continuously*. init() enables those IRQs on whichever core
// calls it. If that's core0, the moment core0 gets busy the scanout stalls and
// next_line_addr freezes → update() spins forever (the blank-screen hang we hit).
//
// So we mirror the shipping MicroPython firmware: dedicate CORE1 to the display
// (init + IRQs live there), and let CORE0 run the app and call update(). This is
// also the architecture we want going forward (core1 = display, core0 = LVGL).
//
// Diagnostic build: cycles full-screen RED/GREEN/BLUE/WHITE/NAVY with labels.

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <cstdio>

using namespace pimoroni;

static const uint FRAME_WIDTH  = 240;
static const uint FRAME_HEIGHT = 240;

static const uint BACKLIGHT = 45;
static const uint LCD_CLK = 26;
static const uint LCD_CS  = 28;
static const uint LCD_DAT = 27;
static const uint LCD_DC  = -1;

static uint16_t back_buffer[FRAME_WIDTH * FRAME_HEIGHT];
static uint16_t front_buffer[FRAME_WIDTH * FRAME_HEIGHT];

static ST7701* g_presto = nullptr;

// Runs on core1: bring the display up here so its scanout IRQs are serviced on
// this core, then stay alive (the IRQs preempt this idle loop in the background).
static void core1_entry() {
    g_presto->init();
    multicore_fifo_push_blocking(1);      // tell core0 init is complete
    while (true) {
        tight_loop_contents();
    }
}

int main() {
    // No set_sys_clock_khz(): the `presto` board header applies its tuned
    // 200MHz PLL at boot, which the ST7701 display timing depends on.
    stdio_init_all();

    for (int i = 0; i < 30; i++) sleep_ms(100);   // let USB CDC enumerate
    printf("\n\n=== SkyFi Screen — Phase 0 (presto board, core1 display) ===\n");
    printf("sys_clk = %lu Hz\n", (unsigned long)clock_get_hz(clk_sys));

    gpio_init(LCD_CS);
    gpio_set_dir(LCD_CS, GPIO_OUT);
    gpio_put(LCD_CS, 1);

    static ST7701 presto(FRAME_WIDTH, FRAME_HEIGHT, ROTATE_0,
                         SPIPins{spi1, LCD_CS, LCD_CLK, LCD_DAT, PIN_UNUSED, LCD_DC, BACKLIGHT},
                         back_buffer);
    static PicoGraphics_PenRGB565 gfx(FRAME_WIDTH, FRAME_HEIGHT, front_buffer);
    g_presto = &presto;

    printf("launching core1 for display scanout...\n");
    multicore_launch_core1(core1_entry);
    uint32_t ok = multicore_fifo_pop_blocking();
    printf("core1 display init done (%lu)\n", (unsigned long)ok);

    presto.set_backlight(255);
    printf("entering render loop\n");

    struct { const char* name; uint8_t r, g, b; } colours[] = {
        {"RED",   255, 0,   0},
        {"GREEN", 0,   255, 0},
        {"BLUE",  0,   0,   255},
        {"WHITE", 255, 255, 255},
        {"NAVY",  11,  15,  26},
    };

    uint32_t frame = 0;
    while (true) {
        auto& c = colours[frame % 5];
        gfx.set_pen(gfx.create_pen(c.r, c.g, c.b));
        gfx.clear();
        gfx.set_pen(gfx.create_pen(0, 0, 0));
        gfx.text(c.name, {20, 20}, FRAME_WIDTH, 4);
        gfx.set_pen(gfx.create_pen(255, 255, 255));
        gfx.text(c.name, {22, 22}, FRAME_WIDTH, 4);

        presto.update(&gfx);

        printf("frame %lu -> %s\n", (unsigned long)frame, c.name);
        frame++;
        sleep_ms(800);
    }
}
