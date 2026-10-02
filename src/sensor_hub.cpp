#include "sensor_hub.hpp"
#include "pilink.hpp"
#include "../ui/ui_panel.hpp"
#include "../common/enviro_hub_regs.h"

#include "common/pimoroni_i2c.hpp"
#include "bme280.hpp"
#include "ltr559.hpp"
#include "hardware/i2c.h"
#include "lvgl.h"

#include <cmath>
#include <cstdio>

using theme::Sev;
using namespace pimoroni;

static const uint QWST_SDA = 40, QWST_SCL = 41;
static const uint8_t BME280_ADDR = 0x77;   // Enviro boards use the alternate address

static const uint32_t SAMPLE_MS      = 1000;
static const uint32_t REPROBE_MS     = 5000;
static const uint32_t HUB_STALE_MS   = 3500;   // seq unchanged this long = stale
static const uint32_t I2C_TIMEOUT_US = 5000;

// Tile severities, matching the server's default auto-land policy.
static const float WIND_WARN = 7, WIND_DANGER = 10, GUST_WARN = 11, GUST_DANGER = 15;

static I2C* s_i2c = nullptr;
static BME280* s_bme = nullptr;
static LTR559* s_ltr = nullptr;
static bool s_bme_ok = false, s_ltr_ok = false, s_hub_ok = false;
static uint32_t s_next_sample = 0, s_next_probe = 0;
static uint8_t s_hub_seq = 0;
static uint32_t s_hub_seq_ms = 0;
static uint32_t s_log_count = 0;

static uint16_t le16(const uint8_t* b, int r) { return b[r] | (b[r + 1] << 8); }
static uint32_t le32(const uint8_t* b, int r) {
    return b[r] | (b[r + 1] << 8) | (b[r + 2] << 16) | ((uint32_t)b[r + 3] << 24);
}

static Sev level(float v, float warn, float danger) {
    return v >= danger ? Sev::DANGER : v >= warn ? Sev::WARN : Sev::OK;
}

static void probe() {
    if (!s_bme_ok) {
        s_bme_ok = s_bme->init();
        printf("sensor_hub: BME280 @ 0x%02x %s\n", BME280_ADDR, s_bme_ok ? "up" : "not found");
    }
    if (!s_ltr_ok) {
        s_ltr_ok = s_ltr->init();
        printf("sensor_hub: LTR-559 @ 0x23 %s\n", s_ltr_ok ? "up" : "not found");
    }
}

// Reads the hub's register block. False if absent or not an Enviro hub.
static bool read_hub(uint8_t* blk) {
    i2c_inst_t* bus = s_i2c->get_i2c();
    uint8_t reg = EHR_WHO_AM_I;
    if (i2c_write_timeout_us(bus, ENVIRO_HUB_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    if (i2c_read_timeout_us(bus, ENVIRO_HUB_ADDR, blk, EHR_BLOCK_LEN, false, I2C_TIMEOUT_US * 4)
        != EHR_BLOCK_LEN) return false;
    return blk[EHR_WHO_AM_I] == ENVIRO_HUB_WHO_AM_I;
}

void sensor_hub_init() {
    s_i2c = new I2C(QWST_SDA, QWST_SCL, 100000);
    s_bme = new BME280(s_i2c, BME280_ADDR);
    s_ltr = new LTR559(s_i2c);
    probe();
    s_next_probe = lv_tick_get() + REPROBE_MS;
}

void sensor_hub_task() {
    uint32_t now = lv_tick_get();
    if ((int32_t)(now - s_next_sample) < 0) return;
    s_next_sample = now + SAMPLE_MS;

    if ((!s_bme_ok || !s_ltr_ok) && (int32_t)(now - s_next_probe) >= 0) {
        probe();
        s_next_probe = now + REPROBE_MS;
    }

    PiWeather wx = pi_weather_empty();
    char buf[24];

    // ── BME280 ──
    if (s_bme_ok) {
        auto r = s_bme->read();
        if (r.status) {
            wx.temp = r.temperature;
            wx.hum = r.humidity;
            wx.pres = r.pressure / 100.0f;   // Pa -> hPa
        } else {
            s_bme_ok = false;
            printf("sensor_hub: BME280 lost\n");
        }
    }
    // ── LTR-559 ──
    if (s_ltr_ok) {
        s_ltr->get_reading();
        wx.lux = s_ltr->data.lux;
    }
    // ── Enviro hub ──
    uint8_t blk[EHR_BLOCK_LEN];
    bool hub_now = read_hub(blk);
    if (hub_now) {
        if (blk[EHR_SEQ] != s_hub_seq || !s_hub_ok) {
            s_hub_seq = blk[EHR_SEQ];
            s_hub_seq_ms = now;
        }
        hub_now = (now - s_hub_seq_ms) < HUB_STALE_MS;
    }
    if (hub_now != s_hub_ok) {
        printf("sensor_hub: Enviro hub @ 0x%02x %s\n", ENVIRO_HUB_ADDR,
               hub_now ? "up" : "lost/stale");
        if (hub_now) printf("sensor_hub: hub v%d status 0x%02x\n", blk[EHR_VERSION], blk[EHR_STATUS]);
    }
    s_hub_ok = hub_now;
    if (s_hub_ok) {
        wx.wind = le16(blk, EHR_WIND) / 100.0f;
        wx.gust = le16(blk, EHR_GUST) / 100.0f;
        uint16_t dir = le16(blk, EHR_DIR);
        if (dir != 0xFFFF) wx.dir = dir;   // unknown stays NaN = omitted
        float rain_hour = le16(blk, EHR_RAIN_HOUR) * ENVIRO_RAIN_MM_PER_TICK;
        wx.rain = rain_hour;          // mm in the last hour
        wx.rain_rate = rain_hour;     // == mm/h over that hour
    }

    // ── dashboard tiles (NaN = "--") ──
    ui_panel_set_metric(Metric::WIND, wx.wind, std::isnan(wx.wind) ? Sev::WARN : level(wx.wind, WIND_WARN, WIND_DANGER));
    ui_panel_set_metric(Metric::GUST, wx.gust, std::isnan(wx.gust) ? Sev::WARN : level(wx.gust, GUST_WARN, GUST_DANGER));
    ui_panel_set_metric(Metric::RAIN, wx.rain_rate, std::isnan(wx.rain_rate) ? Sev::WARN : level(wx.rain_rate, 4, 10));
    ui_panel_set_metric(Metric::TEMP, wx.temp, Sev::OK);
    ui_panel_set_metric(Metric::HUM, wx.hum, Sev::OK);
    ui_panel_set_metric(Metric::PRES, wx.pres, Sev::OK);
    ui_panel_set_metric(Metric::LUX, wx.lux, Sev::OK);

    // Only report fields we actually measured (NaN fields are omitted).
    if (s_hub_ok || s_bme_ok || s_ltr_ok) pilink_send_weather(wx);

    if (s_log_count++ % 10 == 0) {   // human-readable trace every 10 s
        // "trace:" prefix: not parsed by the Pi's legacy sensor_hub line reader
        printf("trace: hub %d bme %d ltr %d wind %.1f gust %.1f dir %.0f temp %.1f lux %.1f\n",
               s_hub_ok, s_bme_ok, s_ltr_ok, (double)wx.wind, (double)wx.gust,
               (double)wx.dir, (double)wx.temp, (double)wx.lux);
    }
}
