// SkyFi Screen — the ground station's physical control panel.
//
// core1 owns the ST7701 display (init + scanout IRQs); core0 runs LVGL.
// Panel: header (PI link + status pill), 2x2 weather tiles from the Enviro
// Weather on Qw/ST (swipe for ground-station status and the WiFi-join QR),
// and the hold-to-confirm SAFETY LAND NOW button.
//
// Links to the Pi: USB serial (pilink, primary — skyfi-app/contracts/
// presto-link.md) and WiFi/REST (net, fallback, when configured at build).

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "drivers/st7701/st7701.hpp"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include <cstdio>

#include "lvgl.h"
#include "lvgl_port.hpp"
#include "peripherals.hpp"
#include "pilink.hpp"
#include "sensor_hub.hpp"
#include "net.hpp"
#include "../ui/ui_panel.hpp"

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

// LAND: USB first (fastest, retried until acked), WiFi if USB is down.
static bool send_land() {
    return pilink_send_land() || net_send_land();
}

static void core1_entry() {
    g_presto->init();
    multicore_fifo_push_blocking(1);
    while (true) {
        tight_loop_contents();   // scanout IRQs preempt this idle loop
    }
}

int main() {
    stdio_init_all();

    printf("\n=== SkyFi Screen ===\n");
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
    ui_panel_create();

    pilink_init();
    sensor_hub_init();
    net_init();                         // WiFi fallback; no-op unless configured
    ui_panel_set_land_handler(send_land);
    printf("panel up, entering main loop\n");

    while (true) {
        uint32_t wait_ms = lv_timer_handler();
        pilink_task();
        sensor_hub_task();
        net_task();
        peripherals_task();
        sleep_ms(wait_ms > 10 ? 10 : wait_ms);
    }
}
