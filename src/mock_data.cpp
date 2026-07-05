// Phase 2: mock environmental data feeding the panel UI. Values wander with
// a random walk; wind gusts occasionally cross the WARN threshold so the
// tile/pill/LED severity path is visible in a demo. Replaced by the REST
// client in Phase 3 (same ui_panel_* setter contract).

#include "mock_data.hpp"
#include "../ui/ui_panel.hpp"
#include "peripherals.hpp"

#include "pico/rand.h"
#include <cstdio>
#include <initializer_list>

using theme::Sev;

static float frand(float lo, float hi) {
    return lo + (hi - lo) * (float)(get_rand_32() & 0xFFFF) / 65535.0f;
}

static float s_wind   = 4.2f;
static float s_temp   = 21.5f;
static float s_tether = 12.4f;
static float s_batt   = 87.0f;

static void mock_tick(lv_timer_t*) {
    // Random walk; wind occasionally gusts
    s_wind += frand(-0.6f, 0.6f);
    if ((get_rand_32() % 12) == 0) s_wind += frand(2.0f, 4.0f);   // gust
    if (s_wind < 0.5f) s_wind = 0.5f;
    if (s_wind > 12.0f) s_wind = 12.0f;
    // decay back toward calm
    s_wind = s_wind * 0.92f + 4.0f * 0.08f;

    s_temp   += frand(-0.15f, 0.15f);
    s_tether += frand(-0.3f, 0.3f);
    if (s_tether < 10.0f) s_tether = 10.0f;
    s_batt   -= 0.01f;

    Sev wind_sev = s_wind > 9.5f ? Sev::DANGER : (s_wind > 7.0f ? Sev::WARN : Sev::OK);
    Sev teth_sev = s_tether > 16.0f ? Sev::WARN : Sev::OK;
    Sev batt_sev = s_batt < 20.0f ? Sev::DANGER : (s_batt < 40.0f ? Sev::WARN : Sev::OK);

    char buf[20];
    snprintf(buf, sizeof(buf), "%.1f m/s", (double)s_wind);
    ui_panel_set_reading(0, "WIND", buf, wind_sev);
    snprintf(buf, sizeof(buf), "%.1f C", (double)s_temp);
    ui_panel_set_reading(1, "TEMP", buf, Sev::OK);
    snprintf(buf, sizeof(buf), "%.1f kg", (double)s_tether);
    ui_panel_set_reading(2, "TETHER", buf, teth_sev);
    snprintf(buf, sizeof(buf), "%.0f %%", (double)s_batt);
    ui_panel_set_reading(3, "BATTERY", buf, batt_sev);

    // Overall = worst severity -> header pill + ambient LEDs
    Sev overall = Sev::OK;
    for (Sev s : {wind_sev, teth_sev, batt_sev}) {
        if ((int)s > (int)overall) overall = s;
    }
    ui_panel_set_status(overall == Sev::OK ? "SYSTEM OK" :
                        overall == Sev::WARN ? "DEGRADED" : "FAULT", overall);

    // LEDs mirror overall state unless a landing alert owns them
    if (!ui_panel_is_landing()) {
        switch (overall) {
            case Sev::OK:     leds_set(8, 28, 0);  break;   // dim green
            case Sev::WARN:   leds_set(46, 26, 0); break;   // amber
            case Sev::DANGER: leds_set(60, 0, 0);  break;   // red
        }
    }
}

void mock_data_start() {
    ui_panel_set_wifi("SkyFi-Ground", "skyfi-field-1234",
                      "WIFI:T:WPA;S:SkyFi-Ground;P:skyfi-field-1234;;");
    mock_tick(nullptr);
    lv_timer_create(mock_tick, 500, nullptr);
}
