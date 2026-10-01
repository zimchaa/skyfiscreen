// Enviro hub I2C register map — shared by enviro/ (the I2C target, on the
// Enviro Weather) and src/sensor_hub.cpp (the I2C controller, on the Presto).
//
// Bus: the Qw/ST cable. The Enviro's own BME280 (0x77) and LTR-559 (0x23)
// sit on the same bus and are read directly by the Presto; the hub only
// serves what needs a microcontroller: anemometer, wind vane and rain gauge.
//
// Protocol: write 1 byte (register address), then read N bytes with
// auto-increment. All multi-byte values are little-endian. The block is
// snapshotted when the register address is written, so a multi-byte read is
// always internally consistent.
#pragma once

#define ENVIRO_HUB_ADDR        0x42
#define ENVIRO_HUB_WHO_AM_I    0xE7
#define ENVIRO_HUB_VERSION     2      // v1 = the lost July 2026 firmware

enum {
    EHR_WHO_AM_I   = 0x00,  // u8  = ENVIRO_HUB_WHO_AM_I
    EHR_VERSION    = 0x01,  // u8  = ENVIRO_HUB_VERSION
    EHR_STATUS     = 0x02,  // u8  EHS_* bits
    EHR_SEQ        = 0x03,  // u8  +1 per sample (1 Hz); unchanged = hub stuck
    EHR_WIND       = 0x04,  // u16 wind speed, cm/s, 3 s average
    EHR_GUST       = 0x06,  // u16 gust, cm/s, max 1 s sample in the last 60 s
    EHR_DIR        = 0x08,  // u16 wind direction, degrees (0xFFFF = unknown)
    EHR_DIR_RAW    = 0x0A,  // u16 vane ADC, 12-bit raw
    EHR_RAIN_TICKS = 0x0C,  // u32 rain-gauge tips since boot
    EHR_RAIN_HOUR  = 0x10,  // u16 rain-gauge tips in the last 60 min
    EHR_UPTIME     = 0x12,  // u32 seconds since boot
    EHR_BLOCK_LEN  = 0x16,
};

enum {
    EHS_ANEMOMETER = 1 << 0,  // anemometer pulses seen since boot
    EHS_VANE_OK    = 1 << 1,  // vane reading matches a known direction
    EHS_WATCHDOG   = 1 << 7,  // hub restarted after a watchdog reset
};

#define ENVIRO_RAIN_MM_PER_TICK 0.2794f
