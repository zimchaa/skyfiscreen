#include "peripherals.hpp"

#include "ws2812.hpp"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

using namespace plasma;

static const uint BUZZER_PIN = 43;
static const uint NUM_LEDS   = 7;
static const uint LED_DAT    = 33;

// Same PIO placement as the shipping MicroPython firmware: pio0 sm3
// (pio1 is fully owned by the display scanout).
static WS2812* s_leds = nullptr;

static uint32_t s_buzzer_off_at = 0;   // ms timestamp; 0 = idle

// LED pulse state: current colour decays toward the base colour.
static uint8_t s_base[3] = {0, 0, 0};
static float s_pulse[3] = {0, 0, 0};
static uint32_t s_last_fade_ms = 0;

void peripherals_init() {
    // Buzzer: PWM, silent until a beep is requested
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(pwm_gpio_to_slice_num(BUZZER_PIN), false);

    static WS2812 leds(NUM_LEDS, pio0, 3, LED_DAT, WS2812::DEFAULT_SERIAL_FREQ,
                       false, WS2812::COLOR_ORDER::GRB);
    s_leds = &leds;
    for (uint i = 0; i < NUM_LEDS; i++) s_leds->set_rgb(i, 0, 0, 0);
    s_leds->update(true);
}

void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0) return;
    uint slice = pwm_gpio_to_slice_num(BUZZER_PIN);

    // Aim for ~1MHz PWM clock, then wrap sets the frequency
    float clkdiv = (float)clock_get_hz(clk_sys) / 1e6f;
    uint32_t wrap = 1000000u / freq_hz;
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, clkdiv);
    pwm_config_set_wrap(&cfg, (uint16_t)wrap);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(BUZZER_PIN, (uint16_t)(wrap / 2));   // 50% duty

    s_buzzer_off_at = to_ms_since_boot(get_absolute_time()) + duration_ms;
}

static void leds_show() {
    for (uint i = 0; i < NUM_LEDS; i++) {
        s_leds->set_rgb(i,
            (uint8_t)MIN(255.0f, s_base[0] + s_pulse[0]),
            (uint8_t)MIN(255.0f, s_base[1] + s_pulse[1]),
            (uint8_t)MIN(255.0f, s_base[2] + s_pulse[2]));
    }
    s_leds->update(true);
}

void leds_set(uint8_t r, uint8_t g, uint8_t b) {
    s_base[0] = r; s_base[1] = g; s_base[2] = b;
    leds_show();
}

void leds_pulse(uint8_t r, uint8_t g, uint8_t b) {
    s_pulse[0] = r; s_pulse[1] = g; s_pulse[2] = b;
    leds_show();
}

void peripherals_task() {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (s_buzzer_off_at && now >= s_buzzer_off_at) {
        pwm_set_enabled(pwm_gpio_to_slice_num(BUZZER_PIN), false);
        s_buzzer_off_at = 0;
    }

    // ~exponential fade of the pulse component, stepped every 20ms
    if (now - s_last_fade_ms >= 20) {
        s_last_fade_ms = now;
        if (s_pulse[0] + s_pulse[1] + s_pulse[2] > 1.0f) {
            for (int i = 0; i < 3; i++) s_pulse[i] *= 0.90f;
            if (s_pulse[0] + s_pulse[1] + s_pulse[2] <= 1.0f) {
                s_pulse[0] = s_pulse[1] = s_pulse[2] = 0;
            }
            leds_show();
        }
    }
}
