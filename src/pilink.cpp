#include "pilink.hpp"
#include "peripherals.hpp"
#include "../ui/ui_panel.hpp"

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "lvgl.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using theme::Sev;

static const uint32_t ONLINE_FOR_MS    = 5000;
static const uint32_t LAND_RETRY_MS    = 1000;
static const uint32_t LAND_GIVE_UP_MS  = 10000;
static const char*    FW_VERSION       = "skyfiscreen 0.4.0";

static char s_line[512];
static size_t s_len = 0;
static bool s_overflow = false;
static bool s_was_connected = false;

static PiStatus s_status = {};
static uint32_t s_last_status_ms = 0;
static bool s_ever_online = false;

static bool s_shown_online = false;

static uint32_t s_land_seq = 0;
static char s_land_id[16] = "";
static uint32_t s_land_started_ms = 0, s_land_next_ms = 0;

// ── tiny flat-JSON field readers (the Pi's messages are flat) ────────

static bool jstr(const char* j, const char* key, char* out, size_t cap) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(j, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool jnum(const char* j, const char* key, float* out) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(j, pat);
    if (!p) return false;
    *out = strtof(p + strlen(pat), nullptr);
    return true;
}

static bool jtrue(const char* j, const char* key) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":true", key);
    return strstr(j, pat) != nullptr;
}

// ── outgoing ─────────────────────────────────────────────────────────

static void send_hello() {
    printf("{\"t\":\"hello\",\"fw\":\"%s\",\"proto\":1}\n", FW_VERSION);
}

static void send_land_now() {
    printf("{\"t\":\"land\",\"id\":\"%s\",\"reason\":\"panel LAND NOW\"}\n", s_land_id);
}

PiWeather pi_weather_empty() {
    return {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};
}

void pilink_send_weather(const PiWeather& w) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "{\"t\":\"wx\"");
    auto field = [&](const char* k, float v, int prec) {
        if (!std::isnan(v) && n < (int)sizeof(buf) - 32)
            n += snprintf(buf + n, sizeof(buf) - n, ",\"%s\":%.*f", k, prec, (double)v);
    };
    field("wind", w.wind, 1);
    field("gust", w.gust, 1);
    field("dir", w.dir, 0);
    field("rain", w.rain, 2);
    field("rain_rate", w.rain_rate, 2);
    field("temp", w.temp, 1);
    field("hum", w.hum, 0);
    field("pres", w.pres, 1);
    field("lux", w.lux, 1);
    printf("%s}\n", buf);
}

bool pilink_send_land() {
    if (!pilink_online()) return false;
    uint32_t now = lv_tick_get();
    snprintf(s_land_id, sizeof(s_land_id), "p-%lu-%lu",
             (unsigned long)(time_us_32() & 0xFFFF), (unsigned long)++s_land_seq);
    s_land_started_ms = now;
    s_land_next_ms = now + LAND_RETRY_MS;
    send_land_now();
    return true;
}

// ── incoming ─────────────────────────────────────────────────────────

static Sev sev_of(const char* sys) {
    if (strcmp(sys, "ok") == 0) return Sev::OK;
    if (strcmp(sys, "degraded") == 0) return Sev::WARN;
    return Sev::DANGER;
}

static void on_status(const char* j) {
    PiStatus st = {};
    float f;
    jstr(j, "sys", st.sys, sizeof(st.sys));
    jstr(j, "drone", st.drone, sizeof(st.drone));
    jstr(j, "power", st.power, sizeof(st.power));
    jstr(j, "auto", st.autoland, sizeof(st.autoland));
    jstr(j, "ip", st.ip, sizeof(st.ip));
    jstr(j, "host", st.host, sizeof(st.host));
    jstr(j, "msg", st.msg, sizeof(st.msg));
    if (jnum(j, "batt", &f)) st.batt = (int)f;
    if (jnum(j, "alt", &f)) st.alt = f;
    if (jnum(j, "tgt", &f)) st.tgt = f;
    if (jnum(j, "tether", &f)) st.tether = f;
    s_status = st;
    s_last_status_ms = lv_tick_get();
    s_ever_online = true;

    // Header pill: drone motion wins, else the server's system state.
    Sev sev = sev_of(st.sys);
    if (strcmp(st.drone, "descending") == 0) {
        ui_panel_set_status("LANDING", Sev::DANGER);
    } else {
        ui_panel_set_status(sev == Sev::OK ? "SYSTEM OK" : sev == Sev::WARN ? "DEGRADED" : "FAULT", sev);
    }

    StationView v = {};
    v.online = true;
    v.sys = sev;
    v.drone = st.drone;
    v.power = st.power;
    v.autoland = strcmp(st.autoland, "armed") == 0;
    v.host = st.host;
    v.ip = st.ip;
    v.msg = st.msg;
    v.alt = st.alt;
    v.tgt = st.tgt;
    v.batt = st.batt;
    v.tether = st.tether;
    ui_panel_set_station(v);

    // Drone telemetry tiles (thresholds mirror the server's defaults).
    Sev batt = st.batt <= 20 ? Sev::DANGER : st.batt <= 40 ? Sev::WARN : Sev::OK;
    Sev teth = st.tether >= 20 ? Sev::DANGER : st.tether >= 16 ? Sev::WARN : Sev::OK;
    ui_panel_set_metric(Metric::BATT, (float)st.batt, batt);
    ui_panel_set_metric(Metric::TETHER, st.tether, teth);
    ui_panel_set_metric(Metric::ALT, st.alt, Sev::OK);

    if (!ui_panel_is_landing()) {
        switch (sev) {
            case Sev::OK:     leds_set(8, 28, 0);  break;   // dim green
            case Sev::WARN:   leds_set(46, 26, 0); break;   // amber
            case Sev::DANGER: leds_set(60, 0, 0);  break;   // red
        }
    }
}

static void on_ack(const char* j) {
    char id[16] = "";
    jstr(j, "id", id, sizeof(id));
    if (!s_land_id[0] || strcmp(id, s_land_id) != 0) return;
    s_land_id[0] = '\0';
    ui_panel_land_result(jtrue(j, "ok"));
}

// A LAND from the app / auto-land: alert exactly like our own button.
static void on_landing(const char* j) {
    char src[16] = "";
    jstr(j, "src", src, sizeof(src));
    if (strcmp(src, "auto") == 0)      ui_panel_remote_land("AUTO-LAND");
    else if (strcmp(src, "web") == 0)  ui_panel_remote_land("LAND FROM APP");
    else                               ui_panel_remote_land("LAND COMMANDED");
}

static void on_wifi(const char* j) {
    char ssid[33] = "", pw[33] = "", qr[128] = "";
    jstr(j, "ssid", ssid, sizeof(ssid));
    jstr(j, "pw", pw, sizeof(pw));
    jstr(j, "qr", qr, sizeof(qr));
    if (ssid[0] && qr[0]) ui_panel_set_wifi(ssid, pw[0] ? pw : "(see QR)", qr);
}

static void on_line(const char* j) {
    char t[12] = "";
    if (j[0] != '{' || !jstr(j, "t", t, sizeof(t))) return;
    if (strcmp(t, "status") == 0)    on_status(j);
    else if (strcmp(t, "ack") == 0)  on_ack(j);
    else if (strcmp(t, "wifi") == 0) on_wifi(j);
    else if (strcmp(t, "landing") == 0) on_landing(j);
}

// ── public ───────────────────────────────────────────────────────────

void pilink_init() {
    s_len = 0;
    s_was_connected = false;
    ui_panel_set_link(false);
    ui_panel_set_status("CONNECTING", Sev::WARN);
}

bool pilink_online() {
    return s_ever_online && (lv_tick_get() - s_last_status_ms) < ONLINE_FOR_MS;
}

const PiStatus& pilink_status() { return s_status; }

void pilink_task() {
    // Host (re)opened the port -> introduce ourselves.
    bool connected = stdio_usb_connected();
    if (connected && !s_was_connected) send_hello();
    s_was_connected = connected;

    // Drain whatever has arrived, without blocking.
    for (int i = 0; i < 256; i++) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) break;
        if (c == '\r') continue;
        if (c == '\n') {
            if (!s_overflow && s_len > 0) {
                s_line[s_len] = '\0';
                on_line(s_line);
            }
            s_len = 0;
            s_overflow = false;
        } else if (s_len + 1 < sizeof(s_line)) {
            s_line[s_len++] = (char)c;
        } else {
            s_overflow = true;   // drop the rest of an over-long line
        }
    }

    // Link indicator + pill when the Pi goes quiet / comes back.
    bool online = pilink_online();
    if (online != s_shown_online) {
        s_shown_online = online;
        ui_panel_set_link(online);
        if (!online) {
            ui_panel_set_status("PI OFFLINE", Sev::DANGER);
            if (!ui_panel_is_landing()) leds_set(60, 0, 0);
            ui_panel_set_metric(Metric::BATT, NAN, Sev::WARN);
            ui_panel_set_metric(Metric::TETHER, NAN, Sev::WARN);
            ui_panel_set_metric(Metric::ALT, NAN, Sev::WARN);
            StationView v = {};
            v.online = false;
            ui_panel_set_station(v);
        }
    }

    // Retry an unacked land with the same id (the Pi de-duplicates).
    if (s_land_id[0]) {
        uint32_t now = lv_tick_get();
        if (now - s_land_started_ms > LAND_GIVE_UP_MS) {
            s_land_id[0] = '\0';
            ui_panel_land_result(false);
        } else if ((int32_t)(now - s_land_next_ms) >= 0) {
            s_land_next_ms = now + LAND_RETRY_MS;
            send_land_now();
        }
    }
}
