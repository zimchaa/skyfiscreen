#include "net.hpp"
#include "http_post.hpp"
#include "peripherals.hpp"
#include "../ui/ui_panel.hpp"

#include "pico/cyw43_arch.h"
#include "lwip/apps/http_client.h"
#include "lwip/ip_addr.h"
#include "lvgl.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

using theme::Sev;

#if defined(SKYFI_WIFI_SSID) && defined(SKYFI_API_HOST)
#define NET_CONFIGURED 1
#else
#define NET_CONFIGURED 0
#define SKYFI_WIFI_SSID     ""
#define SKYFI_WIFI_PASSWORD ""
#define SKYFI_API_HOST      ""
#endif
#ifndef SKYFI_API_PORT
#define SKYFI_API_PORT 8000
#endif

// ── State ────────────────────────────────────────────────────────────

enum class NetState { OFF, JOINING, ONLINE };
static NetState s_state = NetState::OFF;

static uint32_t s_next_action_ms = 0;     // next join-check / poll time
static uint32_t s_last_ok_ms = 0;         // last successful API response
static bool s_wifi_info_fetched = false;
static bool s_poll_env_next = false;      // alternate status/environment
static bool s_get_busy = false;

static const uint32_t POLL_INTERVAL_MS   = 1000;
static const uint32_t STALE_AFTER_MS     = 4000;
static const uint32_t OFFLINE_AFTER_MS   = 12000;
static const uint32_t JOIN_RETRY_MS      = 5000;

// ── JSON-lite helpers (flat contract fields only) ────────────────────

static bool json_str(const char* json, const char* key, char* out, size_t cap) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool json_num(const char* json, const char* key, float* out) {
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p = strchr(p + strlen(pat), ':');
    if (!p) return false;
    *out = strtof(p + 1, nullptr);
    return true;
}

static Sev sev_from_status(const char* s) {
    if (strcmp(s, "ok") == 0) return Sev::OK;
    if (strcmp(s, "warn") == 0 || strcmp(s, "degraded") == 0) return Sev::WARN;
    return Sev::DANGER;   // fault / danger / unknown
}

// ── Response handlers ────────────────────────────────────────────────

static void apply_status(const char* json) {
    char system[16] = "", drone[16] = "";
    json_str(json, "system", system, sizeof(system));
    json_str(json, "drone", drone, sizeof(drone));

    Sev sev = sev_from_status(system);
    const char* text = sev == Sev::OK ? "SYSTEM OK" :
                       sev == Sev::WARN ? "DEGRADED" : "FAULT";
    if (strcmp(drone, "landing") == 0) { text = "LANDING"; sev = Sev::DANGER; }
    ui_panel_set_status(text, sev);

    if (!ui_panel_is_landing()) {
        switch (sev) {
            case Sev::OK:     leds_set(8, 28, 0);  break;
            case Sev::WARN:   leds_set(46, 26, 0); break;
            case Sev::DANGER: leds_set(60, 0, 0);  break;
        }
    }
}

static void apply_environment(const char* json) {
    // readings[] arrive in server order; show the first four.
    const char* p = json;
    for (int idx = 0; idx < UI_NUM_READINGS; idx++) {
        p = strchr(p, '{');
        if (idx == 0 && p) p = strchr(p + 1, '{');   // skip the outer object
        if (!p) break;

        char label[16] = "", unit[8] = "", status[12] = "ok";
        float value = 0;
        // Bound the search to this object so keys don't bleed across readings
        const char* end = strchr(p, '}');
        if (!end) break;
        char obj[192];
        size_t len = (size_t)(end - p + 1);
        if (len >= sizeof(obj)) len = sizeof(obj) - 1;
        memcpy(obj, p, len);
        obj[len] = '\0';

        json_str(obj, "label", label, sizeof(label));
        json_str(obj, "unit", unit, sizeof(unit));
        json_str(obj, "status", status, sizeof(status));
        json_num(obj, "value", &value);

        char text[24];
        snprintf(text, sizeof(text), "%.1f %s", (double)value, unit);
        ui_panel_set_reading(idx, label, text, sev_from_status(status));

        p = end + 1;
    }
}

static void apply_wifi(const char* json) {
    char ssid[33] = "", qr[128] = "", pw[33] = "";
    json_str(json, "ssid", ssid, sizeof(ssid));
    json_str(json, "qr", qr, sizeof(qr));
    json_str(json, "password", pw, sizeof(pw));
    if (ssid[0] && qr[0]) ui_panel_set_wifi(ssid, pw[0] ? pw : "(see QR)", qr);
}

// ── GET plumbing (lwIP bundled http_client) ──────────────────────────

static char s_body[2048];
static size_t s_body_len = 0;
static char s_path[32];

static err_t get_recv_cb(void*, struct altcp_pcb* conn, struct pbuf* p, err_t err) {
    if (!p) return ERR_OK;
    if (err == ERR_OK) {
        size_t space = sizeof(s_body) - 1 - s_body_len;
        size_t n = pbuf_copy_partial(p, s_body + s_body_len,
                                     (u16_t)(p->tot_len < space ? p->tot_len : space), 0);
        s_body_len += n;
        altcp_recved(conn, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

static void get_result_cb(void*, httpc_result_t result, u32_t, u32_t srv_res, err_t) {
    s_get_busy = false;
    if (result != HTTPC_RESULT_OK || srv_res != 200) return;

    s_body[s_body_len] = '\0';
    s_last_ok_ms = lv_tick_get();

    if (strcmp(s_path, "/api/v1/status") == 0)           apply_status(s_body);
    else if (strcmp(s_path, "/api/v1/environment") == 0) apply_environment(s_body);
    else if (strcmp(s_path, "/api/v1/wifi") == 0) {
        apply_wifi(s_body);
        s_wifi_info_fetched = true;
    }
}

static void start_get(const char* path) {
    static httpc_connection_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.result_fn = get_result_cb;

    ip_addr_t addr;
    if (!ipaddr_aton(SKYFI_API_HOST, &addr)) return;

    snprintf(s_path, sizeof(s_path), "%s", path);
    s_body_len = 0;
    s_get_busy = true;
    httpc_state_t* conn = nullptr;
    if (httpc_get_file(&addr, SKYFI_API_PORT, path, &settings,
                       get_recv_cb, nullptr, &conn) != ERR_OK) {
        s_get_busy = false;
    }
}

// ── Land command (raw POST, GET-only library gap) ────────────────────

static void land_post_done(int status, const char* body, size_t) {
    bool accepted = status == 200 && body && strstr(body, "\"accepted\"") &&
                    strstr(body, "true");
    ui_panel_land_result(accepted);
}

bool net_send_land() {
    if (s_state != NetState::ONLINE) return false;
    return http_post(SKYFI_API_HOST, SKYFI_API_PORT, "/api/v1/land",
                     "{\"source\":\"presto-panel\",\"reason\":\"operator_button\",\"confirm\":true}",
                     land_post_done);
}

// ── State machine ────────────────────────────────────────────────────

static void start_join() {
    cyw43_arch_wifi_connect_async(SKYFI_WIFI_SSID, SKYFI_WIFI_PASSWORD,
                                  CYW43_AUTH_WPA2_AES_PSK);
    s_state = NetState::JOINING;
    s_next_action_ms = lv_tick_get() + 500;
    ui_panel_set_status("WIFI...", Sev::WARN);
}

bool net_init() {
#if !NET_CONFIGURED
    printf("net: not configured (set SKYFI_WIFI_SSID/SKYFI_API_HOST), mock mode\n");
    return false;
#else
    if (cyw43_arch_init()) {
        printf("net: cyw43 init failed\n");
        return false;
    }
    cyw43_arch_enable_sta_mode();
    printf("net: joining '%s', API http://%s:%d\n",
           SKYFI_WIFI_SSID, SKYFI_API_HOST, (int)SKYFI_API_PORT);
    start_join();
    return true;
#endif
}

void net_task() {
    if (s_state == NetState::OFF) return;
    cyw43_arch_poll();

    uint32_t now = lv_tick_get();
    if (now < s_next_action_ms) return;

    if (s_state == NetState::JOINING) {
        int st = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (st == CYW43_LINK_UP) {
            printf("net: wifi up\n");
            s_state = NetState::ONLINE;
            s_last_ok_ms = now;
            s_next_action_ms = now;
        } else if (st < 0) {          // auth/join failure -> retry
            printf("net: join failed (%d), retrying\n", st);
            ui_panel_set_status("WIFI FAIL", Sev::DANGER);
            s_next_action_ms = now + JOIN_RETRY_MS;
            start_join();
        } else {
            s_next_action_ms = now + 500;
        }
        return;
    }

    // ONLINE
    if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) != CYW43_LINK_UP) {
        printf("net: wifi lost, rejoining\n");
        ui_panel_set_status("LINK LOST", Sev::DANGER);
        if (!ui_panel_is_landing()) leds_set(60, 0, 0);
        start_join();
        return;
    }

    // Staleness -> pill/LED override (freshness beats last server-said state)
    uint32_t age = now - s_last_ok_ms;
    if (age > OFFLINE_AFTER_MS) {
        ui_panel_set_status("LINK LOST", Sev::DANGER);
        if (!ui_panel_is_landing()) leds_set(60, 0, 0);
    } else if (age > STALE_AFTER_MS) {
        ui_panel_set_status("LINK STALE", Sev::WARN);
        if (!ui_panel_is_landing()) leds_set(46, 26, 0);
    }

    if (!s_get_busy) {
        if (!s_wifi_info_fetched) {
            start_get("/api/v1/wifi");
        } else {
            start_get(s_poll_env_next ? "/api/v1/environment" : "/api/v1/status");
            s_poll_env_next = !s_poll_env_next;
        }
    }
    s_next_action_ms = now + POLL_INTERVAL_MS / 2;   // two endpoints per interval
}
