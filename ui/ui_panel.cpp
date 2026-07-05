#include "ui_panel.hpp"
#include "../src/peripherals.hpp"

#include <cstdio>
#include <cstring>

using theme::Sev;

// ── Layout constants (240x240) ───────────────────────────────────────
static const int HEADER_H = 26;
static const int FOOTER_H = 48;
static const int BODY_H   = 240 - HEADER_H - FOOTER_H;   // 166

static const uint32_t HOLD_TO_LAND_MS = 1500;
static const uint32_t LANDING_SHOW_MS = 5000;

// ── Widgets ──────────────────────────────────────────────────────────
static lv_obj_t* s_pill = nullptr;
static lv_obj_t* s_pill_label = nullptr;

struct Tile {
    lv_obj_t* strip;
    lv_obj_t* name;
    lv_obj_t* value;
};
static Tile s_tiles[UI_NUM_READINGS];

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
static uint32_t s_next_beep_at = 0;
static bool s_led_flash_on = false;
static uint32_t s_next_led_flip = 0;

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

void ui_panel_set_reading(int idx, const char* label, const char* value_text, Sev sev) {
    if (idx < 0 || idx >= UI_NUM_READINGS) return;
    lv_label_set_text(s_tiles[idx].name, label);
    lv_label_set_text(s_tiles[idx].value, value_text);
    lv_obj_set_style_bg_color(s_tiles[idx].strip, theme::sev_color(sev), 0);
}

void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data) {
    lv_qrcode_update(s_qr, qr_data, (uint32_t)strlen(qr_data));
    lv_label_set_text_fmt(s_qr_ssid, "%s", ssid);
    lv_label_set_text_fmt(s_qr_pw, "pw: %s", password);
}

// ── Land button behaviour ────────────────────────────────────────────

static void land_reset_visuals() {
    lv_obj_set_width(s_land_fill, 0);
    lv_label_set_text(s_land_label, "SAFETY LAND NOW");
    lv_obj_set_style_bg_color(s_land_btn, theme::red(), 0);
}

static void land_trigger() {
    s_pressing = false;
    s_landing = true;
    uint32_t now = lv_tick_get();
    s_landing_until = now + LANDING_SHOW_MS;
    s_beeps_left = 3;
    s_next_beep_at = now;
    s_next_led_flip = now;

    lv_obj_set_width(s_land_fill, 0);
    lv_obj_set_style_bg_color(s_land_btn, theme::red_dark(), 0);
    lv_label_set_text(s_land_label, "LAND SENT (mock)");
    apply_pill("LANDING", Sev::DANGER);
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
            lv_obj_set_width(s_land_fill, (int32_t)(240 * held / HOLD_TO_LAND_MS));
            lv_label_set_text(s_land_label, "KEEP HOLDING...");
        }
    }

    if (s_landing) {
        if (s_beeps_left > 0 && now >= s_next_beep_at) {
            buzzer_beep(1760, 120);
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

static void build_header(lv_obj_t* scr) {
    lv_obj_t* bar = make_plain(scr);
    lv_obj_set_size(bar, 240, HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, theme::bg(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t* dot = make_plain(bar);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, 2, 0);
    lv_obj_set_style_bg_color(dot, theme::red(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 8, 0);

    lv_obj_t* brand = lv_label_create(bar);
    lv_label_set_text(brand, "SKY-FI");
    lv_obj_set_style_text_color(brand, theme::text(), 0);
    lv_obj_set_style_text_font(brand, &lv_font_montserrat_14, 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, 22, 0);

    s_pill = make_plain(bar);
    lv_obj_set_size(s_pill, 92, 18);
    lv_obj_set_style_radius(s_pill, 9, 0);
    lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
    lv_obj_align(s_pill, LV_ALIGN_RIGHT_MID, -6, 0);

    s_pill_label = lv_label_create(s_pill);
    lv_obj_set_style_text_color(s_pill_label, lv_color_hex(0x141618), 0);
    lv_obj_set_style_text_font(s_pill_label, &lv_font_montserrat_14, 0);
    lv_obj_center(s_pill_label);
    apply_pill(s_status_text, s_status_sev);
}

static void build_tile(lv_obj_t* parent, int idx, int x, int y, int w, int h) {
    lv_obj_t* card = make_plain(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, theme::surface(), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 6, 0);

    s_tiles[idx].strip = make_plain(card);
    lv_obj_set_size(s_tiles[idx].strip, 4, h - 12);
    lv_obj_set_pos(s_tiles[idx].strip, 6, 6);
    lv_obj_set_style_radius(s_tiles[idx].strip, 2, 0);
    lv_obj_set_style_bg_color(s_tiles[idx].strip, theme::ok(), 0);
    lv_obj_set_style_bg_opa(s_tiles[idx].strip, LV_OPA_COVER, 0);

    s_tiles[idx].name = lv_label_create(card);
    lv_label_set_text(s_tiles[idx].name, "—");
    lv_obj_set_style_text_color(s_tiles[idx].name, theme::text_muted(), 0);
    lv_obj_set_style_text_font(s_tiles[idx].name, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_tiles[idx].name, 18, 10);

    s_tiles[idx].value = lv_label_create(card);
    lv_label_set_text(s_tiles[idx].value, "—");
    lv_obj_set_style_text_color(s_tiles[idx].value, theme::text(), 0);
    lv_obj_set_style_text_font(s_tiles[idx].value, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(s_tiles[idx].value, 18, h - 36);
}

static void build_dashboard(lv_obj_t* page) {
    const int gap = 6;
    const int w = (240 - 3 * gap) / 2;          // 111
    const int h = (BODY_H - 3 * gap) / 2;       // 74
    build_tile(page, 0, gap, gap, w, h);
    build_tile(page, 1, gap * 2 + w, gap, w, h);
    build_tile(page, 2, gap, gap * 2 + h, w, h);
    build_tile(page, 3, gap * 2 + w, gap * 2 + h, w, h);
}

static void build_wifi_page(lv_obj_t* page) {
    lv_obj_t* title = lv_label_create(page);
    lv_label_set_text(title, "JOIN GROUND STATION WIFI");
    lv_obj_set_style_text_color(title, theme::aqua(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    s_qr = lv_qrcode_create(page);
    lv_qrcode_set_size(s_qr, 104);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x141618));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_align(s_qr, LV_ALIGN_CENTER, 0, 2);
    lv_obj_set_style_border_width(s_qr, 4, 0);
    lv_obj_set_style_border_color(s_qr, lv_color_hex(0xFFFFFF), 0);

    s_qr_ssid = lv_label_create(page);
    lv_label_set_text(s_qr_ssid, "—");
    lv_obj_set_style_text_color(s_qr_ssid, theme::text(), 0);
    lv_obj_set_style_text_font(s_qr_ssid, &lv_font_montserrat_14, 0);
    lv_obj_align(s_qr_ssid, LV_ALIGN_BOTTOM_MID, 0, -20);

    s_qr_pw = lv_label_create(page);
    lv_label_set_text(s_qr_pw, "—");
    lv_obj_set_style_text_color(s_qr_pw, theme::text_dim(), 0);
    lv_obj_set_style_text_font(s_qr_pw, &lv_font_montserrat_14, 0);
    lv_obj_align(s_qr_pw, LV_ALIGN_BOTTOM_MID, 0, -4);
}

static void build_land_button(lv_obj_t* scr) {
    s_land_btn = make_plain(scr);
    lv_obj_set_size(s_land_btn, 240, FOOTER_H);
    lv_obj_set_pos(s_land_btn, 0, 240 - FOOTER_H);
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

    s_land_label = lv_label_create(s_land_btn);
    lv_obj_set_style_text_color(s_land_label, theme::text(), 0);
    lv_obj_set_style_text_font(s_land_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_land_label);
    land_reset_visuals();

    lv_timer_create(land_timer_cb, 30, nullptr);
}

void ui_panel_create() {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, theme::bg(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);

    // Swipeable body: dashboard <-> wifi QR
    lv_obj_t* tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, 240, BODY_H);
    lv_obj_set_pos(tv, 0, HEADER_H);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* dash = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    lv_obj_t* wifi = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    build_dashboard(dash);
    build_wifi_page(wifi);

    build_land_button(scr);
}
