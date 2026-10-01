// Enviro hub — firmware for the Pimoroni Enviro Weather, plugged into the
// Presto's Qw/ST port. Turns the weather kit's anemometer, wind vane and rain
// gauge into I2C registers (../common/enviro_hub_regs.h) that the Presto polls.
//
// The Enviro's BME280 and LTR-559 share the Qw/ST bus and are read by the
// Presto directly, so this board never drives the bus as a controller.
//
// Calibration follows pimoroni/enviro boards/weather.py.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/i2c_slave.h"
#include "pico/stdio_usb.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#include "enviro_hub_regs.h"

// ── Enviro Weather pins ──────────────────────────────────────────────
#define PIN_HOLD_VSYS_EN  2    // keep the board powered when on battery
#define PIN_I2C_SDA       4    // Qw/ST
#define PIN_I2C_SCL       5
#define PIN_ACTIVITY_LED  6
#define PIN_WIND_SPEED    9    // anemometer reed switch (pull-up)
#define PIN_RAIN          10   // rain gauge reed switch (pull-down)
#define PIN_WIND_DIR      26   // vane resistor network, ADC0
#define ADC_WIND_DIR      0

// ── calibration (pimoroni/enviro) ────────────────────────────────────
#define WIND_CM_RADIUS    7.0f
#define WIND_FACTOR       0.0218f
// Vane voltage for each 45° step, index * 45 = degrees.
static const float VANE_VOLTS[8] = {0.9f, 2.0f, 3.0f, 2.8f, 2.5f, 1.5f, 0.3f, 0.6f};
#define VANE_MATCH_VOLTS  0.15f   // further than this from every entry = unknown

#define WIND_DEBOUNCE_US  1000
#define RAIN_DEBOUNCE_US  100000

// ── pulse counting (GPIO IRQ) ────────────────────────────────────────
static volatile uint32_t s_wind_edges = 0, s_rain_ticks = 0;
static volatile uint64_t s_wind_last_us = 0, s_rain_last_us = 0;

static void gpio_cb(uint gpio, uint32_t events) {
    uint64_t now = time_us_64();
    if (gpio == PIN_WIND_SPEED) {
        if (now - s_wind_last_us >= WIND_DEBOUNCE_US) s_wind_edges++;
        s_wind_last_us = now;
    } else if (gpio == PIN_RAIN && (events & GPIO_IRQ_EDGE_RISE)) {
        if (now - s_rain_last_us >= RAIN_DEBOUNCE_US) s_rain_ticks++;
        s_rain_last_us = now;
    }
}

// ── register block + I2C target ──────────────────────────────────────
static uint8_t s_live[EHR_BLOCK_LEN];     // updated by the main loop
static uint8_t s_shadow[EHR_BLOCK_LEN];   // what the controller reads
static uint8_t s_reg = 0;
static bool s_reg_set = false;

static void put16(uint8_t* b, int reg, uint16_t v) { b[reg] = v & 0xFF; b[reg + 1] = v >> 8; }
static void put32(uint8_t* b, int reg, uint32_t v) {
    for (int i = 0; i < 4; i++) b[reg + i] = (v >> (8 * i)) & 0xFF;
}

static void i2c_handler(i2c_inst_t* i2c, i2c_slave_event_t event) {
    switch (event) {
    case I2C_SLAVE_RECEIVE: {
        uint8_t b = i2c_read_byte_raw(i2c);
        if (!s_reg_set) {   // first written byte = register address
            s_reg = b;
            s_reg_set = true;
            memcpy(s_shadow, s_live, sizeof(s_shadow));
        }                   // further written bytes: no writable registers
        break;
    }
    case I2C_SLAVE_REQUEST:
        i2c_write_byte_raw(i2c, s_reg < EHR_BLOCK_LEN ? s_shadow[s_reg] : 0xFF);
        s_reg++;
        break;
    case I2C_SLAVE_FINISH:
        s_reg_set = false;
        break;
    default:
        break;
    }
}

// ── sampling ─────────────────────────────────────────────────────────

static int vane_degrees(uint16_t raw) {
    float v = raw * 3.3f / 4095.0f;
    int best = -1;
    float best_d = VANE_MATCH_VOLTS;
    for (int i = 0; i < 8; i++) {
        float d = fabsf(VANE_VOLTS[i] - v);
        if (d < best_d) { best_d = d; best = i; }
    }
    return best < 0 ? -1 : best * 45;
}

static float edges_to_ms(uint32_t edges_per_s) {
    // Two edges per reed-switch cycle, as in weather.py's tick averaging.
    float rotation_hz = edges_per_s / 2.0f;
    return rotation_hz * (WIND_CM_RADIUS * 2.0f * (float)M_PI) * WIND_FACTOR;
}

int main() {
    stdio_init_all();

    gpio_init(PIN_HOLD_VSYS_EN);
    gpio_set_dir(PIN_HOLD_VSYS_EN, GPIO_OUT);
    gpio_put(PIN_HOLD_VSYS_EN, 1);

    gpio_init(PIN_ACTIVITY_LED);
    gpio_set_dir(PIN_ACTIVITY_LED, GPIO_OUT);
    gpio_put(PIN_ACTIVITY_LED, 1);

    gpio_init(PIN_WIND_SPEED);
    gpio_pull_up(PIN_WIND_SPEED);
    gpio_init(PIN_RAIN);
    gpio_pull_down(PIN_RAIN);
    gpio_set_irq_enabled_with_callback(PIN_WIND_SPEED, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, gpio_cb);
    gpio_set_irq_enabled(PIN_RAIN, GPIO_IRQ_EDGE_RISE, true);

    adc_init();
    adc_gpio_init(PIN_WIND_DIR);
    adc_select_input(ADC_WIND_DIR);

    bool after_watchdog = watchdog_enable_caused_reboot();   // not picotool/BOOTSEL reboots
    uint8_t status_base = after_watchdog ? EHS_WATCHDOG : 0;

    s_live[EHR_WHO_AM_I] = ENVIRO_HUB_WHO_AM_I;
    s_live[EHR_VERSION] = ENVIRO_HUB_VERSION;
    put16(s_live, EHR_DIR, 0xFFFF);

    gpio_init(PIN_I2C_SDA);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SDA);
    gpio_init(PIN_I2C_SCL);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_I2C_SCL);
    i2c_init(i2c0, 400 * 1000);
    i2c_slave_init(i2c0, ENVIRO_HUB_ADDR, i2c_handler);

    watchdog_enable(3000, true);

    float wind_1s[60] = {0};      // 1 s samples, ring (gust = max)
    uint16_t rain_min[60] = {0};  // tips per minute, ring (rain last hour)
    uint32_t sample = 0, seq = 0, last_edges = 0, last_rain = 0;
    bool anemometer_seen = false;
    absolute_time_t next = make_timeout_time_ms(1000);
    sleep_ms(200);
    gpio_put(PIN_ACTIVITY_LED, 0);

    while (true) {
        watchdog_update();
        sleep_until(next);
        next = delayed_by_ms(next, 1000);

        uint32_t edges = s_wind_edges, rain = s_rain_ticks;
        uint32_t de = edges - last_edges, dr = rain - last_rain;
        last_edges = edges;
        last_rain = rain;
        if (de) anemometer_seen = true;

        wind_1s[sample % 60] = edges_to_ms(de);
        if (sample % 60 == 0) rain_min[(sample / 60) % 60] = 0;
        rain_min[(sample / 60) % 60] += (uint16_t)dr;
        sample++;

        int n = sample < 3 ? (int)sample : 3;
        float avg = 0, gust = 0;
        for (int i = 0; i < n; i++) avg += wind_1s[(sample - 1 - i) % 60];
        avg /= n;
        int span = sample < 60 ? (int)sample : 60;
        for (int i = 0; i < span; i++) gust = fmaxf(gust, wind_1s[i]);
        uint32_t rain_hour = 0;
        for (int i = 0; i < 60; i++) rain_hour += rain_min[i];

        // Average a few ADC reads; the vane is a resistor ladder.
        uint32_t acc = 0;
        for (int i = 0; i < 8; i++) acc += adc_read();
        uint16_t raw = acc / 8;
        int dir = vane_degrees(raw);

        uint8_t st = status_base | (anemometer_seen ? EHS_ANEMOMETER : 0) | (dir >= 0 ? EHS_VANE_OK : 0);

        uint32_t irq = save_and_disable_interrupts();   // keep the I2C snapshot coherent
        s_live[EHR_STATUS] = st;
        s_live[EHR_SEQ] = (uint8_t)++seq;
        put16(s_live, EHR_WIND, (uint16_t)(avg * 100.0f + 0.5f));
        put16(s_live, EHR_GUST, (uint16_t)(gust * 100.0f + 0.5f));
        put16(s_live, EHR_DIR, dir < 0 ? 0xFFFF : (uint16_t)dir);
        put16(s_live, EHR_DIR_RAW, raw);
        put32(s_live, EHR_RAIN_TICKS, rain);
        put16(s_live, EHR_RAIN_HOUR, (uint16_t)(rain_hour > 0xFFFF ? 0xFFFF : rain_hour));
        put32(s_live, EHR_UPTIME, sample);
        restore_interrupts(irq);

        if (dr) {   // blink on each rain tip
            gpio_put(PIN_ACTIVITY_LED, 1);
            sleep_ms(30);
            gpio_put(PIN_ACTIVITY_LED, 0);
        }
        if (stdio_usb_connected()) {
            printf("enviro-hub v%d: wind %.2f m/s gust %.2f dir %d raw %u rain %lu tips (%lu/h) st 0x%02x%s\n",
                   ENVIRO_HUB_VERSION, (double)avg, (double)gust, dir, raw,
                   (unsigned long)rain, (unsigned long)rain_hour, st,
                   after_watchdog ? " [after watchdog]" : "");
        }
    }
}
