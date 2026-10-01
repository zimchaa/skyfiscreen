// The SkyFi ground-station panel UI (Phase 2: mocked data).
//
// Layout (240x240 logical):
//   header  — brand + system status pill
//   body    — swipeable: [dashboard: 2x2 env tiles] [ground station] [wifi QR]
//   footer  — full-width SAFETY LAND NOW hold-to-confirm button
#pragma once

#include "theme.hpp"

// Number of environmental tiles on the dashboard.
static const int UI_NUM_READINGS = 4;

void ui_panel_create();

// idx 0..3; value_text e.g. "4.2 m/s". Severity colours the tile accent.
void ui_panel_set_reading(int idx, const char* label, const char* value_text, theme::Sev sev);

// Header pill, e.g. ("SYSTEM OK", OK) / ("DEGRADED", WARN).
void ui_panel_set_status(const char* text, theme::Sev sev);

// Header "PI" indicator: is the ground station (USB link) talking to us?
void ui_panel_set_link(bool online);

// Ground-station page: multi-line text block + severity accent.
void ui_panel_set_station(const char* text, theme::Sev sev);

// WiFi join info for the QR page. qr_data is a WIFI: URI.
void ui_panel_set_wifi(const char* ssid, const char* password, const char* qr_data);

// True while a land command sequence is active (mock: brief "SENT" state).
bool ui_panel_is_landing();

// Async land dispatcher (USB pilink, then WiFi). The hold-to-confirm shows
// "SENDING" and waits for ui_panel_land_result(); the handler returns false
// if the command could not even be dispatched (no link). When unset, the
// mock "LAND SENT" flow runs (UI development only).
void ui_panel_set_land_handler(bool (*handler)());
void ui_panel_land_result(bool accepted);
