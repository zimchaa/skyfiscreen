// The SkyFi ground-station panel UI, native 480x480.
//
// Layout:
//   header   — brand, PI link indicator, system status pill
//   tabs     — segmented DASH | TRENDS | STATION | WIFI (tap; swipe also works)
//   body     — DASH: 3x3 metric tiles, each with value + 5-minute sparkline
//              TRENDS: metric list with live values; last hour of the
//                      selected one as a large chart
//              STATION: drone state, altitude/battery/tether gauges, host/IP
//              WIFI: QR code to join the ground-station WiFi
//   footer   — full-width SAFETY LAND NOW hold-to-confirm button
#pragma once

#include "theme.hpp"

// Metrics, in tile order (row-major, 3x3); ALT is trends-only.
enum class Metric { WIND, GUST, RAIN, TEMP, HUM, PRES, LUX, BATT, TETHER, ALT, COUNT };

void ui_panel_create();

// Latest value for a metric (NaN = not available -> "--"). Severity colours
// the tile accent. History for sparklines/trends is sampled from these.
void ui_panel_set_metric(Metric m, float value, theme::Sev sev);

// Header pill, e.g. ("SYSTEM OK", OK) / ("DEGRADED", WARN).
void ui_panel_set_status(const char* text, theme::Sev sev);

// Header "PI" indicator: is the ground station (USB link) talking to us?
void ui_panel_set_link(bool online);

// STATION tab content (from the Pi's status; online=false shows PI OFFLINE).
struct StationView {
    bool online;
    theme::Sev sys;
    const char* drone;      // grounded | ascending | airborne | descending
    const char* power;      // tether | battery
    bool autoland;
    const char* host;
    const char* ip;
    const char* msg;        // top alert, may be ""
    float alt, tgt;         // m
    int batt;               // %
    float tether;           // kg
};
void ui_panel_set_station(const StationView& v);

// WiFi join info for the WIFI tab. qr_data is a WIFI: URI.
void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data);

// True while a land command sequence is active.
bool ui_panel_is_landing();

// Async land dispatcher (USB pilink, then WiFi). The hold-to-confirm shows
// "SENDING" and waits for ui_panel_land_result(); the handler returns false
// if the command could not even be dispatched (no link).
void ui_panel_set_land_handler(bool (*handler)());
void ui_panel_land_result(bool accepted);

// A LAND commanded elsewhere (app, auto-land): same beeps/flash/LANDING as
// pressing the panel's own button, with `text` on the button.
void ui_panel_remote_land(const char* text);
