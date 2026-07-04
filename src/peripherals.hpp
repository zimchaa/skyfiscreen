// Presto peripherals: piezo buzzer (GPIO 43), 7x WS2812 ambient LEDs
// (GPIO 33), backlight passthrough.
#pragma once

#include <cstdint>

void peripherals_init();

// Non-blocking beep: starts now, peripherals_task() ends it.
void buzzer_beep(uint32_t freq_hz, uint32_t duration_ms);

// Set all 7 ambient LEDs; brightness fades back down in peripherals_task().
void leds_pulse(uint8_t r, uint8_t g, uint8_t b);
void leds_set(uint8_t r, uint8_t g, uint8_t b);

// Call frequently from the main loop (ends beeps, fades LED pulses).
void peripherals_task();
