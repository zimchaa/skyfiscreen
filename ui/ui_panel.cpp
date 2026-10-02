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
static const int TABBAR_H = 52;
static const int TABS_H   = 480 - HEADER_H - FOOTER_H;   // 328, incl. tab bar
static const int BODY_H   = TABS_H - TABBAR_H;           // 276
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

struct MetricInfo {
    const char* name;       // tile label
    const char* tab;        // TRENDS button (short)
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
};

static float s_latest[N_METRICS];
static Sev s_sev[N_METRICS];
static int32_t* s_hist[N_METRICS];      // HIST_N each, in the LVGL (PSRAM) heap

// ── Widgets ──────────────────────────────────────────────────────────
static lv_obj_t* s_pill = nullptr;
static lv_obj_t* s_pill_label = nullptr;
static lv_obj_t* s_link = nullptr;

struct Tile {
    lv_obj_t* strip;
    lv_obj_t* value;
    lv_obj_t* chart;
    lv_chart_series_t* ser;
};
static Tile s_tiles[N_METRICS];

static lv_obj_t* s_trend_chart = nullptr;
static lv_chart_series_t* s_trend_ser = nullptr;
static lv_obj_t* s_trend_title = nullptr;
static lv_obj_t* s_trend_max = nullptr;
static lv_obj_t* s_trend_min = nullptr;
static int s_trend_metric = 0;

static lv_obj_t* s_station = nullptr;
static lv_obj_t* s_station_strip = nullptr;

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

void ui_panel_set_station(const char* text, Sev sev) {
    lv_label_set_text(s_station, text);
    lv_obj_set_style_bg_color(s_station_strip, theme::sev_color(sev), 0);
}

void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data) {
    lv_qrcode_update(s_qr, qr_data, (uint32_t)strlen(qr_data));
    lv_label_set_text_fmt(s_qr_ssid, "%s", ssid);
    lv_label_set_text_fmt(s_qr_pw, "password: %s", password);
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
    const MetricInfo& mi = METRICS[s_trend_metric];
    int32_t lo, hi;
    lv_chart_set_ext_y_array(s_trend_chart, s_trend_ser, s_hist[s_trend_metric]);
    fit_range(s_trend_chart, s_hist[s_trend_metric], HIST_N, mi.min_span, &lo, &hi);
    lv_chart_refresh(s_trend_chart);

    char v[24] = "--";
    if (!std::isnan(s_latest[s_trend_metric])) snprintf(v, sizeof(v), mi.fmt, (double)s_latest[s_trend_metric]);
    lv_label_set_text_fmt(s_trend_title, "%s  %s   (last 60 min)", mi.name, v);
    bool any = false;
    for (int i = 0; i < HIST_N && !any; i++) any = s_hist[s_trend_metric][i] != LV_CHART_POINT_NONE;
    if (any) {
        snprintf(v, sizeof(v), mi.fmt, hi / 10.0);
        lv_label_set_text_fmt(s_trend_max, "max %s", v);
        snprintf(v, sizeof(v), mi.fmt, lo / 10.0);
        lv_label_set_text_fmt(s_trend_min, "min %s", v);
    } else {
        lv_label_set_text(s_trend_max, "");
        lv_label_set_text(s_trend_min, "no data yet");
    }
}

static void sample_timer_cb(lv_timer_t*) {
    for (int m = 0; m < N_METRICS; m++) {
        int32_t* h = s_hist[m];
        memmove(h, h + 1, (HIST_N - 1) * sizeof(int32_t));
        h[HIST_N - 1] = std::isnan(s_latest[m]) ? LV_CHART_POINT_NONE
                                                : (int32_t)lroundf(s_latest[m] * 10.0f);
        fit_range(s_tiles[m].chart, h + HIST_N - SPARK_N, SPARK_N, METRICS[m].min_span);
        lv_chart_refresh(s_tiles[m].chart);
    }
    refresh_trend();
}

void ui_panel_set_metric(Metric metric, float value, Sev sev) {
    int m = (int)metric;
    if (m < 0 || m >= N_METRICS) return;
    s_latest[m] = value;
    s_sev[m] = sev;
    char buf[24];
    if (std::isnan(value)) {
        lv_label_set_text(s_tiles[m].value, "--");
        lv_obj_set_style_bg_color(s_tiles[m].strip, theme::text_dim(), 0);
    } else {
        snprintf(buf, sizeof(buf), METRICS[m].fmt, (double)value);
        lv_label_set_text(s_tiles[m].value, buf);
        lv_obj_set_style_bg_color(s_tiles[m].strip, theme::sev_color(sev), 0);
    }
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
    for (int m = 0; m < N_METRICS; m++) {
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

static void trend_select_cb(lv_event_t* e) {
    lv_obj_t* bm = (lv_obj_t*)lv_event_get_target(e);
    uint32_t sel = lv_buttonmatrix_get_selected_button(bm);
    if (sel < (uint32_t)N_METRICS) {
        s_trend_metric = (int)sel;
        refresh_trend();
    }
}

static void build_trends(lv_obj_t* page) {
    static const char* map[N_METRICS + 1];
    for (int m = 0; m < N_METRICS; m++) map[m] = METRICS[m].tab;
    map[N_METRICS] = "";

    lv_obj_t* bm = lv_buttonmatrix_create(page);
    lv_buttonmatrix_set_map(bm, map);
    lv_buttonmatrix_set_button_ctrl_all(bm, LV_BUTTONMATRIX_CTRL_CHECKABLE);
    lv_buttonmatrix_set_one_checked(bm, true);
    lv_buttonmatrix_set_button_ctrl(bm, 0, LV_BUTTONMATRIX_CTRL_CHECKED);
    lv_obj_set_size(bm, W - 2 * GAP, 44);
    lv_obj_set_pos(bm, GAP, GAP);
    lv_obj_set_style_pad_all(bm, 2, 0);
    lv_obj_set_style_pad_gap(bm, 3, 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_bg_color(bm, theme::surface(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, theme::text_muted(), LV_PART_ITEMS);
    lv_obj_set_style_text_font(bm, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bm, theme::teal(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(bm, theme::text(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(bm, trend_select_cb, LV_EVENT_VALUE_CHANGED, nullptr);

    const int card_y = GAP + 44 + GAP;
    const int card_h = BODY_H - card_y - GAP;
    lv_obj_t* card = make_card(page, GAP, card_y, W - 2 * GAP, card_h);

    s_trend_title = make_label(card, "", &lv_font_montserrat_20, theme::text());
    lv_obj_set_pos(s_trend_title, 14, 8);

    s_trend_chart = make_line_chart(card, HIST_N);
    lv_obj_set_size(s_trend_chart, W - 2 * GAP - 28, card_h - 66);
    lv_obj_set_pos(s_trend_chart, 14, 38);
    lv_chart_set_div_line_count(s_trend_chart, 4, 6);
    lv_obj_set_style_line_color(s_trend_chart, theme::surface_hi(), LV_PART_MAIN);
    lv_obj_set_style_line_width(s_trend_chart, 3, LV_PART_ITEMS);
    s_trend_ser = lv_chart_add_series(s_trend_chart, theme::aqua(), LV_CHART_AXIS_PRIMARY_Y);

    s_trend_max = make_label(card, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(s_trend_max, LV_ALIGN_TOP_RIGHT, -14, 12);
    s_trend_min = make_label(card, "", &lv_font_montserrat_14, theme::text_muted());
    lv_obj_align(s_trend_min, LV_ALIGN_BOTTOM_LEFT, 14, -6);
    lv_obj_t* now_lbl = make_label(card, "-60 min                                     now",
                                   &lv_font_montserrat_14, theme::text_dim());
    lv_obj_align(now_lbl, LV_ALIGN_BOTTOM_RIGHT, -14, -6);

    refresh_trend();
}

static void build_station(lv_obj_t* page) {
    lv_obj_t* card = make_card(page, GAP, GAP, W - 2 * GAP, BODY_H - 2 * GAP);

    s_station_strip = make_plain(card);
    lv_obj_set_size(s_station_strip, 6, BODY_H - 2 * GAP - 24);
    lv_obj_set_pos(s_station_strip, 10, 12);
    lv_obj_set_style_radius(s_station_strip, 3, 0);
    lv_obj_set_style_bg_color(s_station_strip, theme::danger(), 0);
    lv_obj_set_style_bg_opa(s_station_strip, LV_OPA_COVER, 0);

    s_station = make_label(card, "Waiting for the Pi\n(USB link)...", &lv_font_montserrat_20, theme::text());
    lv_obj_set_width(s_station, W - 2 * GAP - 48);
    lv_label_set_long_mode(s_station, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_line_space(s_station, 6, 0);
    lv_obj_set_pos(s_station, 30, 14);
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

static void style_tab_bar(lv_obj_t* tv) {
    lv_obj_t* bar = lv_tabview_get_tab_bar(tv);
    lv_obj_set_style_bg_color(bar, theme::bg(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, GAP, 0);
    lv_obj_set_style_pad_gap(bar, GAP, 0);
    uint32_t n = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t* b = lv_obj_get_child(bar, (int32_t)i);
        lv_obj_set_style_bg_color(b, theme::surface(), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_margin_ver(b, 6, 0);
        lv_obj_set_style_text_font(b, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(b, theme::text_muted(), 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, theme::teal(), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, theme::text(), LV_STATE_CHECKED);
        lv_obj_set_style_border_width(b, 0, LV_STATE_CHECKED);
    }
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

    lv_obj_t* tv = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tv, TABBAR_H);
    lv_obj_set_size(tv, W, TABS_H);
    lv_obj_set_pos(tv, 0, HEADER_H);
    lv_obj_set_style_bg_color(tv, theme::bg(), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);

    lv_obj_t* dash = lv_tabview_add_tab(tv, "DASH");
    lv_obj_t* trends = lv_tabview_add_tab(tv, "TRENDS");
    lv_obj_t* station = lv_tabview_add_tab(tv, "STATION");
    lv_obj_t* wifi = lv_tabview_add_tab(tv, "WIFI");
    for (lv_obj_t* p : {dash, trends, station, wifi}) {
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    }
    style_tab_bar(tv);

    build_dashboard(dash);
    build_trends(trends);
    build_station(station);
    build_wifi(wifi);
    build_land_button(scr);

    lv_timer_create(sample_timer_cb, SAMPLE_MS, nullptr);
}
