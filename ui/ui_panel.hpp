// The SkyFi ground-station panel UI, native 480x480.
//
// Layout:
//   header   — brand, PI link indicator, system status pill
//   tab bar  — DASH | TRENDS | STATION | WIFI (tap; swipe also works)
//   body     — DASH: 3x3 metric tiles, each with value + 5-minute sparkline
//              TRENDS: pick a metric, last hour as a large chart
//              STATION: ground-station status from the Pi
//              WIFI: QR code to join the ground-station WiFi
//   footer   — full-width SAFETY LAND NOW hold-to-confirm button
#pragma once

#include "theme.hpp"

// Dashboard metrics, in tile order (row-major, 3x3).
enum class Metric { WIND, GUST, RAIN, TEMP, HUM, PRES, LUX, BATT, TETHER, COUNT };

void ui_panel_create();

// Latest value for a metric (NaN = not available -> "--"). Severity colours
// the tile accent. History for sparklines/trends is sampled from these.
void ui_panel_set_metric(Metric m, float value, theme::Sev sev);

// Header pill, e.g. ("SYSTEM OK", OK) / ("DEGRADED", WARN).
void ui_panel_set_status(const char* text, theme::Sev sev);

// Header "PI" indicator: is the ground station (USB link) talking to us?
void ui_panel_set_link(bool online);

// STATION tab: multi-line text block + severity accent.
void ui_panel_set_station(const char* text, theme::Sev sev);

// WiFi join info for the WIFI tab. qr_data is a WIFI: URI.
void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data);

// True while a land command sequence is active.
bool ui_panel_is_landing();

// Async land dispatcher (USB pilink, then WiFi). The hold-to-confirm shows
// "SENDING" and waits for ui_panel_land_result(); the handler returns false
// if the command could not even be dispatched (no link).
void ui_panel_set_land_handler(bool (*handler)());
void ui_panel_land_result(bool accepted);
