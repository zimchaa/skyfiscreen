#include "ui_panel.hpp"
#include "../src/peripherals.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

using theme::Sev;

// ── Layout (480x480) ─────────────────────────────────────────────────
static const int W        = 480;
static const int HEADER_H = 56;
static const int FOOTER_H = 96;
static const int SEG_H    = 52;                                  // segmented tabs
static const int BODY_H   = 480 - HEADER_H - SEG_H - FOOTER_H;   // 276
static const int GAP      = 8;

static const uint32_t HOLD_TO_LAND_MS = 1500;
static const uint32_t LANDING_SHOW_MS = 5000;

// ── Metric history ───────────────────────────────────────────────────
// Sampled every SAMPLE_MS: sparklines show the last SPARK_N samples (5 min),
// TRENDS the full HIST_N (1 h). Values are stored x10 as chart integers.
static const uint32_t SAMPLE_MS = 5000;
static const int SPARK_N = 60;
static const int HIST_N  = 720;
static const int N_METRICS = (int)Metric::COUNT;
static const int N_TILES = 9;            // ALT is trends-only

struct MetricInfo {
    const char* name;       // tile label
    const char* tab;        // short selector label
    const char* fmt;        // printf for the value
    int32_t min_span;       // smallest y range shown (x10), so noise stays flat
};
static const MetricInfo METRICS[N_METRICS] = {
    {"WIND",     "WIND", "%.1f m/s",  20},
    {"GUST",     "GUST", "%.1f m/s",  20},
    {"RAIN",     "RAIN", "%.1f mm/h", 10},
    {"TEMP",     "TEMP", "%.1f C",    20},
    {"HUMIDITY", "HUM",  "%.0f %%",   50},
    {"PRESSURE", "PRES", "%.0f hPa",  20},
    {"LIGHT",    "LUX",  "%.0f lux", 100},
    {"BATTERY",  "BATT", "%.0f %%",  100},
    {"TETHER",   "TETH", "%.1f kg",   20},
    {"ALTITUDE", "ALT",  "%.0f m",   100},
};

static float s_latest[N_METRICS];
static Sev s_sev[N_METRICS];
static int32_t* s_hist[N_METRICS];      // HIST_N each, in the LVGL (PSRAM) heap

static void fmt_metric(int m, char* buf, size_t n) {
    if (std::isnan(s_latest[m])) snprintf(buf, n, "--");
    else snprintf(buf, n, METRICS[m].fmt, (double)s_latest[m]);
}

// ── Widgets ──────────────────────────────────────────────────────────
static lv_obj_t* s_pill = nullptr;
static lv_obj_t* s_pill_label = nullptr;
static lv_obj_t* s_link = nullptr;
static lv_obj_t* s_tabview = nullptr;
static lv_obj_t* s_seg = nullptr;

struct Tile {
    lv_obj_t* strip;
    lv_obj_t* value;
    lv_obj_t* chart;
    lv_chart_series_t* ser;
};
static Tile s_tiles[N_TILES];

// TRENDS: metric list (live values) beside a chart card.
struct TrendView {
    lv_obj_t* chart;
    lv_chart_series_t* ser;
    lv_obj_t* title;
    lv_obj_t* max;
    lv_obj_t* min;
};
static TrendView s_tv;
static int s_trend_metric = 0;
static lv_obj_t* s_list_btn[N_METRICS];
static lv_obj_t* s_list_val[N_METRICS];

// STATION
static lv_obj_t* s_st_state = nullptr;
static lv_obj_t* s_st_auto = nullptr;
static lv_obj_t* s_st_auto_lbl = nullptr;
static lv_obj_t* s_st_altbar = nullptr;
static lv_obj_t* s_st_tgt = nullptr;
static lv_obj_t* s_st_alt = nullptr;
static lv_obj_t* s_st_alt_sub = nullptr;
static lv_obj_t* s_st_batt_arc = nullptr;
static lv_obj_t* s_st_batt = nullptr;
static lv_obj_t* s_st_power = nullptr;
static lv_obj_t* s_st_teth_arc = nullptr;
static lv_obj_t* s_st_teth = nullptr;
static lv_obj_t* s_st_host = nullptr;
static lv_obj_t* s_st_msg = nullptr;
static const int ALTBAR_H = 104;
static int s_altbar_y = 0;

static lv_obj_t* s_qr = nullptr;
static lv_obj_t* s_qr_ssid = nullptr;
static lv_obj_t* s_qr_pw = nullptr;

static lv_obj_t* s_land_btn = nullptr;
static lv_obj_t* s_land_fill = nullptr;
static lv_obj_t* s_land_label = nullptr;

// ── Land button state ────────────────────────────────────────────────
static bool s_pressing = false;
static uint32_t s_press_start = 0;
static bool s_landing = false;
static uint32_t s_landing_until = 0;
static int s_beeps_left = 0;
static uint32_t s_beep_freq = 1760;
static uint32_t s_next_beep_at = 0;
static bool s_led_flash_on = false;
static uint32_t s_next_led_flip = 0;
static bool (*s_land_handler)() = nullptr;

void ui_panel_set_land_handler(bool (*handler)()) { s_land_handler = handler; }

// Last status set by the app, so we can restore it after a landing alert.
static char s_status_text[24] = "STARTING";
static Sev s_status_sev = Sev::WARN;

bool ui_panel_is_landing() { return s_landing; }

static void apply_pill(const char* text, Sev sev) {
    lv_label_set_text(s_pill_label, text);
    lv_obj_set_style_bg_color(s_pill, theme::sev_color(sev), 0);
}

void ui_panel_set_status(const char* text, Sev sev) {
    snprintf(s_status_text, sizeof(s_status_text), "%s", text);
    s_status_sev = sev;
    if (!s_landing) apply_pill(text, sev);
}

void ui_panel_set_link(bool online) {
    lv_obj_set_style_text_color(s_link, online ? theme::ok() : theme::danger(), 0);
}

void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data) {
    lv_qrcode_update(s_qr, qr_data, (uint32_t)strlen(qr_data));
    lv_label_set_text_fmt(s_qr_ssid, "%s", ssid);
    lv_label_set_text_fmt(s_qr_pw, "password: %s", password);
}

// ── STATION ──────────────────────────────────────────────────────────

void ui_panel_set_station(const StationView& v) {
    char buf[64];
    if (!v.online) {
        lv_label_set_text(s_st_state, "PI OFFLINE");
        lv_obj_set_style_text_color(s_st_state, theme::danger(), 0);
        lv_obj_add_flag(s_st_auto, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_value(s_st_altbar, 0, LV_ANIM_OFF);
        lv_label_set_text(s_st_alt, "--");
        lv_label_set_text(s_st_alt_sub, "");
        lv_arc_set_value(s_st_batt_arc, 0);
        lv_label_set_text(s_st_batt, "--");
        lv_label_set_text(s_st_power, "");
        lv_arc_set_value(s_st_teth_arc, 0);
        lv_label_set_text(s_st_teth, "--");
        lv_label_set_text(s_st_host, "No status from the Pi over USB");
        lv_label_set_text(s_st_msg, "");
        return;
    }

    // Drone state, coloured by motion
    snprintf(buf, sizeof(buf), "%s", v.drone);
    for (char* c = buf; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
    lv_label_set_text(s_st_state, buf);
    lv_color_t sc = theme::text();
    if (!strcmp(v.drone, "airborne")) sc = theme::ok();
    else if (!strcmp(v.drone, "ascending")) sc = theme::aqua();
    else if (!strcmp(v.drone, "descending")) sc = theme::danger();
    lv_obj_set_style_text_color(s_st_state, sc, 0);

    lv_obj_remove_flag(s_st_auto, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_st_auto_lbl, v.autoland ? "AUTO-LAND ARMED" : "AUTO-LAND OFF");
    lv_obj_set_style_bg_color(s_st_auto, v.autoland ? theme::ok() : theme::danger(), 0);

    // Altitude bar scaled to 125% of the target, with the target marked
    float top = fmaxf(v.tgt * 1.25f, 10.0f);
    lv_bar_set_value(s_st_altbar, (int32_t)fminf(1000.0f, v.alt / top * 1000.0f), LV_ANIM_ON);
    int ty = s_altbar_y + ALTBAR_H - (int)(v.tgt / top * ALTBAR_H) - 2;
    lv_obj_set_y(s_st_tgt, ty);
    // (LVGL's own formatter has no float support: format with snprintf)
    snprintf(buf, sizeof(buf), "%.0f m", (double)v.alt);
    lv_label_set_text(s_st_alt, buf);
    snprintf(buf, sizeof(buf), "target %.0f m", (double)v.tgt);
    lv_label_set_text(s_st_alt_sub, buf);

    Sev bs = v.batt <= 20 ? Sev::DANGER : v.batt <= 40 ? Sev::WARN : Sev::OK;
    lv_arc_set_value(s_st_batt_arc, v.batt);
    lv_obj_set_style_arc_color(s_st_batt_arc, theme::sev_color(bs), LV_PART_INDICATOR);
    lv_label_set_text_fmt(s_st_batt, "%d%%", v.batt);
    bool on_batt = !strcmp(v.power, "battery");
    lv_label_set_text(s_st_power, on_batt ? "ON BATTERY" : "tether power");
    lv_obj_set_style_text_color(s_st_power, on_batt ? theme::warn() : theme::text_muted(), 0);

    Sev ts = v.tether >= 20 ? Sev::DANGER : v.tether >= 16 ? Sev::WARN : Sev::OK;
    lv_arc_set_value(s_st_teth_arc, (int32_t)(v.tether * 10));
    lv_obj_set_style_arc_color(s_st_teth_arc, ts == Sev::OK ? theme::aqua() : theme::sev_color(ts), LV_PART_INDICATOR);
    snprintf(buf, sizeof(buf), "%.1f kg", (double)v.tether);
    lv_label_set_text(s_st_teth, buf);

    lv_label_set_text_fmt(s_st_host, "%s  %s", v.host, v.ip[0] ? v.ip : "no network");
    lv_label_set_text(s_st_msg, v.msg);
    lv_obj_set_style_text_color(s_st_msg, theme::sev_color(v.sys), 0);
}

// ── Charts ───────────────────────────────────────────────────────────

// Fit the y range to the points in view (at least min_span tall).
static void fit_range(lv_obj_t* chart, const int32_t* pts, int n, int32_t min_span,
                      int32_t* out_lo = nullptr, int32_t* out_hi = nullptr) {
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int i = 0; i < n; i++) {
        if (pts[i] == LV_CHART_POINT_NONE) continue;
        if (pts[i] < lo) lo = pts[i];
        if (pts[i] > hi) hi = pts[i];
    }
    if (lo > hi) { lo = 0; hi = min_span; }          // no data yet
    if (hi - lo < min_span) {
        int32_t mid = (lo + hi) / 2;
        lo = mid - min_span / 2;
        hi = lo + min_span;
    }
    int32_t pad = (hi - lo) / 10 + 1;
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, lo - pad, hi + pad);
    if (out_lo) *out_lo = lo;
    if (out_hi) *out_hi = hi;
}

static void refresh_trend() {
    TrendView& tv = s_tv;
    const MetricInfo& mi = METRICS[s_trend_metric];
    int32_t lo, hi;
    lv_chart_set_ext_y_array(tv.chart, tv.ser, s_hist[s_trend_metric]);
    fit_range(tv.chart, s_hist[s_trend_metric], HIST_N, mi.min_span, &lo, &hi);
    lv_chart_refresh(tv.chart);

    char v[24];
    fmt_metric(s_trend_metric, v, sizeof(v));
    lv_label_set_text_fmt(tv.title, "%s  %s", mi.name, v);
    bool any = false;
    for (int i = 0; i < HIST_N && !any; i++) any = s_hist[s_trend_metric][i] != LV_CHART_POINT_NONE;
    if (any) {
        snprintf(v, sizeof(v), mi.fmt, hi / 10.0);
        lv_label_set_text_fmt(tv.max, "max %s", v);
        snprintf(v, sizeof(v), mi.fmt, lo / 10.0);
        lv_label_set_text_fmt(tv.min, "min %s", v);
    } else {
        lv_label_set_text(tv.max, "");
        lv_label_set_text(tv.min, "no data yet");
    }
}

// Live values beside the names (LIST layout).
static void refresh_list_values() {
    char buf[24];
    for (int m = 0; m < N_METRICS; m++) {
        fmt_metric(m, buf, sizeof(buf));
        lv_label_set_text(s_list_val[m], buf);
    }
}

static void sample_timer_cb(lv_timer_t*) {
    for (int m = 0; m < N_METRICS; m++) {
        int32_t* h = s_hist[m];
        memmove(h, h + 1, (HIST_N - 1) * sizeof(int32_t));
        h[HIST_N - 1] = std::isnan(s_latest[m]) ? LV_CHART_POINT_NONE
                                                : (int32_t)lroundf(s_latest[m] * 10.0f);
        if (m < N_TILES) {
            fit_range(s_tiles[m].chart, h + HIST_N - SPARK_N, SPARK_N, METRICS[m].min_span);
            lv_chart_refresh(s_tiles[m].chart);
        }
    }
    refresh_trend();
    refresh_list_values();
}

void ui_panel_set_metric(Metric metric, float value, Sev sev) {
    int m = (int)metric;
    if (m < 0 || m >= N_METRICS) return;
    s_latest[m] = value;
    s_sev[m] = sev;
    if (m >= N_TILES) return;
    char buf[24];
    fmt_metric(m, buf, sizeof(buf));
    lv_label_set_text(s_tiles[m].value, buf);
    lv_obj_set_style_bg_color(s_tiles[m].strip,
                              std::isnan(value) ? theme::text_dim() : theme::sev_color(sev), 0);
}

// ── Land button behaviour ────────────────────────────────────────────

static void land_reset_visuals() {
    lv_obj_set_width(s_land_fill, 0);
    lv_label_set_text(s_land_label, "SAFETY LAND NOW");
    lv_obj_set_style_bg_color(s_land_btn, theme::red(), 0);
}

static void land_alert(const char* text, int beeps, uint32_t beep_freq, uint32_t show_ms) {
    uint32_t now = lv_tick_get();
    s_landing = true;
    s_landing_until = now + show_ms;
    s_beeps_left = beeps;
    s_beep_freq = beep_freq;
    s_next_beep_at = now;
    s_next_led_flip = now;
    lv_obj_set_width(s_land_fill, 0);
    lv_obj_set_style_bg_color(s_land_btn, theme::red_dark(), 0);
    lv_label_set_text(s_land_label, text);
}

void ui_panel_remote_land(const char* text) {
    if (s_landing) return;              // already alerting (e.g. our own press)
    s_pressing = false;
    land_alert(text, 3, 1760, LANDING_SHOW_MS);
    apply_pill("LANDING", Sev::DANGER);
}

void ui_panel_land_result(bool accepted) {
    if (accepted) {
        land_alert("LAND ACCEPTED", 3, 1760, LANDING_SHOW_MS);
        apply_pill("LANDING", Sev::DANGER);
    } else {
        land_alert("LAND CMD FAILED", 2, 440, 3000);
        apply_pill("LINK FAIL", Sev::DANGER);
    }
}

static void land_trigger() {
    s_pressing = false;
    uint32_t now = lv_tick_get();
    if (!s_land_handler) {
        ui_panel_land_result(false);
        return;
    }
    // Dispatch and wait for ui_panel_land_result(); failsafe timeout in case
    // no result ever arrives.
    s_landing = true;
    s_landing_until = now + 12000;
    s_beeps_left = 0;
    s_next_led_flip = now;
    lv_obj_set_width(s_land_fill, 0);
    lv_obj_set_style_bg_color(s_land_btn, theme::red_dark(), 0);
    lv_label_set_text(s_land_label, "SENDING LAND CMD...");
    apply_pill("LANDING", Sev::DANGER);
    if (!s_land_handler()) ui_panel_land_result(false);
}

static void land_event_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (s_landing) return;   // ignore input during the alert window

    if (code == LV_EVENT_PRESSED) {
        s_pressing = true;
        s_press_start = lv_tick_get();
        buzzer_beep(660, 30);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_pressing = false;
        land_reset_visuals();
    }
}

// 30ms UI tick: hold progress, landing alert pattern + timeout.
static void land_timer_cb(lv_timer_t*) {
    uint32_t now = lv_tick_get();

    if (s_pressing) {
        uint32_t held = now - s_press_start;
        if (held >= HOLD_TO_LAND_MS) {
            land_trigger();
        } else {
            lv_obj_set_width(s_land_fill, (int32_t)(W * held / HOLD_TO_LAND_MS));
            lv_label_set_text(s_land_label, "KEEP HOLDING...");
        }
    }

    if (s_landing) {
        if (s_beeps_left > 0 && now >= s_next_beep_at) {
            buzzer_beep(s_beep_freq, 120);
            s_next_beep_at = now + 250;
            s_beeps_left--;
        }
        if (now >= s_next_led_flip) {
            s_led_flash_on = !s_led_flash_on;
            leds_set(s_led_flash_on ? 200 : 20, 0, 0);
            s_next_led_flip = now + 250;
        }
        if (now >= s_landing_until) {
            s_landing = false;
            land_reset_visuals();
            apply_pill(s_status_text, s_status_sev);
        }
    }
}


// ── Construction ─────────────────────────────────────────────────────

static lv_obj_t* make_plain(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    return o;
}

static lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

static lv_obj_t* make_card(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* card = make_plain(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);
    return card;
}

static lv_obj_t* make_line_chart(lv_obj_t* parent, int n_points) {
    lv_obj_t* c = lv_chart_create(parent);
    lv_obj_remove_style_all(c);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, n_points);
    lv_chart_set_div_line_count(c, 0, 0);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_width(c, 0, LV_PART_INDICATOR);     // no point markers
    lv_obj_set_style_height(c, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

// Segmented-control styling shared by the main tabs and trends selectors:
// a rounded track, items flat, the checked item raised in teal with an aqua
// underline.
static void style_segmented(lv_obj_t* bm, const lv_font_t* font, int radius) {
    lv_obj_set_style_bg_color(bm, theme::surface(), 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bm, radius + 4, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, 4, 0);
    lv_obj_set_style_pad_gap(bm, 4, 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, radius, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(bm, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, font, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, theme::text_muted(), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bm, theme::teal(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bm, theme::text(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(bm, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(bm, 3, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(bm, theme::aqua(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(bm, theme::surface_hi(), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(bm, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
}

static lv_obj_t* make_segmented(lv_obj_t* parent, const char* const* map, const lv_font_t* font, int radius) {
    lv_obj_t* bm = lv_buttonmatrix_create(parent);
    lv_buttonmatrix_set_map(bm, map);
    lv_buttonmatrix_set_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(bm, true);
    lv_buttonmatrix_set_button_ctrl(bm, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    style_segmented(bm, font, radius);
    return bm;
}

static void build_header(lv_obj_t* scr) {
    lv_obj_t* bar = make_plain(scr);
    lv_obj_set_size(bar, W, HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::bg(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t* dot = make_plain(bar);
    lv_obj_set_size(dot, 16, 16);
    lv_obj_set_style_radius(dot, 4, 0);
    lv_obj_set_style_bg_color(dot, theme::red(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t* brand = make_label(bar, "SKY-FI", &lv_font_montserrat_28, theme::text());
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 42, 0);

    s_pill = make_plain(bar);
    lv_obj_set_size(s_pill, 196, 38);
    lv_obj_set_style_radius(s_pill, 19, 0);
    lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
    lv_obj_align(s_pill, LV_ALIGN_RIGHT_MID, -12, 0);
    s_pill_label = make_label(s_pill, "", &lv_font_montserrat_20, lv_color_hex(0x141618));
    lv_obj_center(s_pill_label);

    s_link = make_label(bar, "PI", &lv_font_montserrat_24, theme::danger());
    lv_obj_align(s_link, LV_ALIGN_RIGHT_MID, -224, 0);

    apply_pill(s_status_text, s_status_sev);
}

static void build_dashboard(lv_obj_t* page) {
    const int w = (W - 4 * GAP) / 3;          // 149
    const int h = (BODY_H - 4 * GAP) / 3;     // 81
    for (int m = 0; m < N_TILES; m++) {
        int col = m % 3, row = m / 3;
        lv_obj_t* card = make_card(page, GAP + col * (w + GAP), GAP + row * (h + GAP), w, h);
        Tile& t = s_tiles[m];

        t.strip = make_plain(card);
        lv_obj_set_size(t.strip, 5, h - 16);
        lv_obj_set_pos(t.strip, 7, 8);
        lv_obj_set_style_radius(t.strip, 2, 0);
        lv_obj_set_style_bg_color(t.strip, theme::text_dim(), 0);
        lv_obj_set_style_bg_opa(t.strip, LV_OPA_COVER, 0);

        lv_obj_t* name = make_label(card, METRICS[m].name, &lv_font_montserrat_14, theme::text_muted());
        lv_obj_set_pos(name, 20, 6);
        t.value = make_label(card, "--", &lv_font_montserrat_24, theme::text());
        lv_obj_set_pos(t.value, 20, 24);

        t.chart = make_line_chart(card, SPARK_N);
        lv_obj_set_size(t.chart, w - 28, 22);
        lv_obj_set_pos(t.chart, 20, h - 26);
        t.ser = lv_chart_add_series(t.chart, theme::aqua(), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_ext_y_array(t.chart, t.ser, s_hist[m] + HIST_N - SPARK_N);
    }
}

// ── TRENDS ───────────────────────────────────────────────────────────

// Chart card: title, chart with min/max, time axis.
static void build_trend_card(TrendView& tv, lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* card = make_card(parent, x, y, w, h);

    tv.title = make_label(card, "", &lv_font_montserrat_20, theme::text());
    lv_obj_set_pos(tv.title, 12, 10);

    tv.chart = make_line_chart(card, HIST_N);
    lv_obj_set_size(tv.chart, w - 24, h - 72);
    lv_obj_set_pos(tv.chart, 12, 44);
    lv_chart_set_div_line_count(tv.chart, 4, 6);
    lv_obj_set_style_line_color(tv.chart, theme::surface_hi(), LV_PART_MAIN);
    lv_obj_set_style_line_width(tv.chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(tv.chart, 3, LV_PART_ITEMS);
    tv.ser = lv_chart_add_series(tv.chart, theme::aqua(), LV_CHART_AXIS_PRIMARY_Y);

    tv.max = make_label(card, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_set_pos(tv.max, 14, 46);
    tv.min = make_label(card, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(tv.min, LV_ALIGN_BOTTOM_LEFT, 12, -6);
    lv_obj_t* t = make_label(card, "-60 min", &lv_font_montserrat_14, theme::text_dim());
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -6);
    t = make_label(card, "now", &lv_font_montserrat_14, theme::text_dim());
    lv_obj_align(t, LV_ALIGN_BOTTOM_RIGHT, -12, -6);
}

static void select_trend_metric(int m) {
    s_trend_metric = m;
    for (int i = 0; i < N_METRICS; i++) {
        if (i == m) lv_obj_add_state(s_list_btn[i], LV_STATE_CHECKED);
        else lv_obj_remove_state(s_list_btn[i], LV_STATE_CHECKED);
    }
    lv_obj_scroll_to_view(s_list_btn[m], LV_ANIM_ON);
    refresh_trend();
}

static void list_cb(lv_event_t* e) {
    select_trend_metric((int)(intptr_t)lv_event_get_user_data(e));
}

// Scrolling list of metrics with live values, chart beside it.
static void build_trends(lv_obj_t* page) {
    const int lw = 168;
    lv_obj_t* list = make_plain(page);
    lv_obj_set_size(list, lw, BODY_H - 2 * GAP);
    lv_obj_set_pos(list, GAP, GAP);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    for (int m = 0; m < N_METRICS; m++) {
        lv_obj_t* b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, lw, 48);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, theme::surface(), 0);
        lv_obj_set_style_bg_color(b, theme::teal(), LV_STATE_CHECKED);
        lv_obj_set_style_border_side(b, LV_BORDER_SIDE_LEFT, LV_STATE_CHECKED);
        lv_obj_set_style_border_width(b, 4, LV_STATE_CHECKED);
        lv_obj_set_style_border_color(b, theme::aqua(), LV_STATE_CHECKED);
        lv_obj_add_event_cb(b, list_cb, LV_EVENT_CLICKED, (void*)(intptr_t)m);
        lv_obj_t* n = make_label(b, METRICS[m].tab, &lv_font_montserrat_14, theme::text_muted());
        lv_obj_set_style_text_color(n, theme::text(), LV_STATE_CHECKED);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 12, 0);
        s_list_val[m] = make_label(b, "--", &lv_font_montserrat_18, theme::text());
        lv_obj_align(s_list_val[m], LV_ALIGN_RIGHT_MID, -10, 0);
        s_list_btn[m] = b;
    }
    build_trend_card(s_tv, page, GAP * 2 + lw, GAP, W - 3 * GAP - lw, BODY_H - 2 * GAP);
    select_trend_metric(0);
}

// ── STATION ──────────────────────────────────────────────────────────

static lv_obj_t* make_gauge_arc(lv_obj_t* parent, int x, int y, int size, int32_t max) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, x, y);
    lv_arc_set_bg_angles(a, 135, 45);           // 270-degree gauge, open at the bottom
    lv_arc_set_range(a, 0, max);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(a, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, theme::surface_hi(), LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, theme::ok(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

static void build_station(lv_obj_t* page) {
    const int cw = W - 2 * GAP, ch = BODY_H - 2 * GAP;   // 464 x 260
    lv_obj_t* card = make_card(page, GAP, GAP, cw, ch);

    // Top row: drone state + auto-land chip
    s_st_state = make_label(card, "WAITING FOR PI", &lv_font_montserrat_28, theme::text_muted());
    lv_obj_set_pos(s_st_state, 16, 10);
    s_st_auto = make_plain(card);
    lv_obj_set_size(s_st_auto, 178, 30);
    lv_obj_align(s_st_auto, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_set_style_radius(s_st_auto, 8, 0);
    lv_obj_set_style_bg_opa(s_st_auto, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_st_auto, theme::surface_hi(), 0);
    s_st_auto_lbl = make_label(s_st_auto, "", &lv_font_montserrat_14, lv_color_hex(0x141618));
    lv_obj_center(s_st_auto_lbl);
    lv_obj_add_flag(s_st_auto, LV_OBJ_FLAG_HIDDEN);

    // Three gauge columns
    const int col_w = cw / 3, gy = 54;
    const char* titles[3] = {"ALTITUDE", "BATTERY", "TETHER"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t* t = make_label(card, titles[i], &lv_font_montserrat_14, theme::text_muted());
        lv_obj_set_width(t, col_w);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(t, i * col_w, gy);
    }
    const int gauge_y = gy + 22, arc = 112;

    // Altitude: vertical bar + target marker, value to the right
    s_altbar_y = gauge_y + 4;
    s_st_altbar = lv_bar_create(card);
    lv_obj_set_size(s_st_altbar, 30, ALTBAR_H);
    lv_obj_set_pos(s_st_altbar, 30, s_altbar_y);
    lv_bar_set_range(s_st_altbar, 0, 1000);
    lv_obj_set_style_radius(s_st_altbar, 8, 0);
    lv_obj_set_style_radius(s_st_altbar, 8, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_st_altbar, theme::surface_hi(), 0);
    lv_obj_set_style_bg_opa(s_st_altbar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_st_altbar, theme::aqua(), LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_st_altbar, 600, 0);
    s_st_tgt = make_plain(card);
    lv_obj_set_size(s_st_tgt, 44, 4);
    lv_obj_set_pos(s_st_tgt, 23, s_altbar_y + ALTBAR_H);
    lv_obj_set_style_radius(s_st_tgt, 2, 0);
    lv_obj_set_style_bg_color(s_st_tgt, theme::text(), 0);
    lv_obj_set_style_bg_opa(s_st_tgt, LV_OPA_COVER, 0);
    s_st_alt = make_label(card, "--", &lv_font_montserrat_24, theme::text());
    lv_obj_set_pos(s_st_alt, 74, gauge_y + 30);
    s_st_alt_sub = make_label(card, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_set_pos(s_st_alt_sub, 74, gauge_y + 62);

    // Battery + tether: 270-degree arcs with the value in the middle
    s_st_batt_arc = make_gauge_arc(card, col_w + (col_w - arc) / 2, gauge_y, arc, 100);
    s_st_batt = make_label(s_st_batt_arc, "--", &lv_font_montserrat_24, theme::text());
    lv_obj_center(s_st_batt);
    s_st_power = make_label(s_st_batt_arc, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(s_st_power, LV_ALIGN_BOTTOM_MID, 0, 0);

    s_st_teth_arc = make_gauge_arc(card, 2 * col_w + (col_w - arc) / 2, gauge_y, arc, 250);
    s_st_teth = make_label(s_st_teth_arc, "--", &lv_font_montserrat_20, theme::text());
    lv_obj_center(s_st_teth);
    lv_obj_t* of = make_label(s_st_teth_arc, "of 25 kg", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(of, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Bottom: host + IP, top alert
    lv_obj_t* rule = make_plain(card);
    lv_obj_set_size(rule, cw - 32, 1);
    lv_obj_set_pos(rule, 16, ch - 48);
    lv_obj_set_style_bg_color(rule, theme::surface_hi(), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    s_st_host = make_label(card, "", &lv_font_montserrat_18, theme::text());
    lv_obj_set_pos(s_st_host, 16, ch - 36);
    s_st_msg = make_label(card, "", &lv_font_montserrat_14, theme::warn());
    lv_obj_set_width(s_st_msg, 230);
    lv_label_set_long_mode(s_st_msg, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_st_msg, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_st_msg, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
}

static void build_wifi(lv_obj_t* page) {
    lv_obj_t* title = make_label(page, "JOIN GROUND STATION WIFI", &lv_font_montserrat_18, theme::aqua());
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_qr = lv_qrcode_create(page);
    lv_qrcode_set_size(s_qr, 176);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x141618));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_align(s_qr, LV_ALIGN_TOP_MID, 0, 38);
    lv_obj_set_style_border_width(s_qr, 6, 0);
    lv_obj_set_style_border_color(s_qr, lv_color_hex(0xFFFFFF), 0);

    s_qr_ssid = make_label(page, "--", &lv_font_montserrat_18, theme::text());
    lv_obj_align(s_qr_ssid, LV_ALIGN_BOTTOM_MID, 0, -26);
    s_qr_pw = make_label(page, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(s_qr_pw, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void build_land_button(lv_obj_t* scr) {
    s_land_btn = make_plain(scr);
    lv_obj_set_size(s_land_btn, W, FOOTER_H);
    lv_obj_set_pos(s_land_btn, 0, 480 - FOOTER_H);
    lv_obj_set_style_bg_color(s_land_btn, theme::red(), 0);
    lv_obj_set_style_bg_opa(s_land_btn, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_land_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_land_btn, land_event_cb, LV_EVENT_ALL, nullptr);

    // Progress fill that grows across the button while held
    s_land_fill = make_plain(s_land_btn);
    lv_obj_set_size(s_land_fill, 0, FOOTER_H);
    lv_obj_set_pos(s_land_fill, 0, 0);
    lv_obj_set_style_bg_color(s_land_fill, lv_color_hex(0xFF4040), 0);
    lv_obj_set_style_bg_opa(s_land_fill, LV_OPA_COVER, 0);

    s_land_label = make_label(s_land_btn, "", &lv_font_montserrat_40, theme::text());
    lv_obj_center(s_land_label);
    land_reset_visuals();

    lv_timer_create(land_timer_cb, 30, nullptr);
}

// Segmented tabs drive the tabview; swiping the tabview drives the tabs.
static void seg_cb(lv_event_t* e) {
    uint32_t sel = lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e));
    if (sel < 4) lv_tabview_set_active(s_tabview, sel, LV_ANIM_ON);
}
static void tabview_cb(lv_event_t*) {
    lv_buttonmatrix_set_button_ctrl(s_seg, lv_tabview_get_tab_active(s_tabview), LV_BUTTONMATRIX_CTRL_CHECKED);
}

void ui_panel_create() {
    for (int m = 0; m < N_METRICS; m++) {
        s_latest[m] = NAN;
        s_sev[m] = Sev::WARN;
        s_hist[m] = (int32_t*)lv_malloc(HIST_N * sizeof(int32_t));
        for (int i = 0; i < HIST_N; i++) s_hist[m][i] = LV_CHART_POINT_NONE;
    }

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, theme::bg(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);

    static const char* tabs[] = {"DASH", "TRENDS", "STATION", "WIFI", ""};
    s_seg = make_segmented(scr, tabs, &lv_font_montserrat_18, 10);
    lv_obj_set_size(s_seg, W - 2 * GAP, SEG_H - 6);
    lv_obj_set_pos(s_seg, GAP, HEADER_H);
    lv_obj_add_event_cb(s_seg, seg_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    // Tabview for the pages + swipe; its own tab bar is hidden.
    s_tabview = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_size(s_tabview, 0);
    lv_obj_add_flag(lv_tabview_get_tab_bar(s_tabview), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(s_tabview, W, BODY_H);
    lv_obj_set_pos(s_tabview, 0, HEADER_H + SEG_H);
    lv_obj_set_style_bg_color(s_tabview, theme::bg(), 0);
    lv_obj_set_style_bg_opa(s_tabview, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_tabview, tabview_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t* dash = lv_tabview_add_tab(s_tabview, "DASH");
    lv_obj_t* trends = lv_tabview_add_tab(s_tabview, "TRENDS");
    lv_obj_t* station = lv_tabview_add_tab(s_tabview, "STATION");
    lv_obj_t* wifi = lv_tabview_add_tab(s_tabview, "WIFI");
    for (lv_obj_t* p : {dash, trends, station, wifi}) {
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    }

    build_dashboard(dash);
    build_trends(trends);
    build_station(station);
    build_wifi(wifi);
    build_land_button(scr);

    lv_timer_create(sample_timer_cb, SAMPLE_MS, nullptr);
}
