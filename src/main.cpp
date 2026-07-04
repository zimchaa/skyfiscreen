// SkyFi Screen — Phase 0: display bring-up
//
// Architecture (mirrors the shipping MicroPython firmware):
//   core1 — owns the ST7701 display: init() runs there, so the PIO scanout
//           IRQs (timing feed + end-of-line/frame) are serviced on core1.
//   core0 — application: renders with PicoGraphics and calls update(), which
//           copies the front buffer into the scanout back buffer racing the
//           beam. From Phase 1 this is where LVGL lives.
//
// Note: requires the start_frame_xfer() fix in lib/presto's ST7701 driver
// (see patches/) — without it the first frame boundary hangs the display core.

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include <cstdio>

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

int main() {
    // No set_sys_clock_khz(): the `presto` board header applies its tuned
    // 200MHz PLL at boot, which the ST7701 display timing was validated on.
    stdio_init_all();

    printf("\n=== SkyFi Screen — Phase 0 ===\n");
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
    printf("display up, entering render loop\n");

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
        gfx.text("SkyFi Screen", {20, 20}, FRAME_WIDTH, 3);
        gfx.text(c.name, {20, 60}, FRAME_WIDTH, 4);
        gfx.set_pen(gfx.create_pen(255, 255, 255));
        gfx.text("SkyFi Screen", {21, 21}, FRAME_WIDTH, 3);
        gfx.text(c.name, {22, 62}, FRAME_WIDTH, 4);

        presto.update(&gfx);

        printf("frame %lu -> %s\n", (unsigned long)frame, c.name);
        frame++;
        sleep_ms(800);
    }
}
