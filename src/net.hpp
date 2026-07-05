// WiFi + REST client against the SkyFi ground-station API (Phase 3).
//
// GET polling uses lwIP's bundled HTTP client (pico_lwip_http); the land
// command uses a minimal raw-TCP POST (http_post.cpp) since lwIP's client
// is GET-only.
//
// Build-time config (CMake cache vars -> compile definitions):
//   SKYFI_WIFI_SSID / SKYFI_WIFI_PASSWORD  — AP to join
//   SKYFI_API_HOST / SKYFI_API_PORT        — REST server (the Pi / mock)
// If unset, net_init() returns false and the app should fall back to mock data.
#pragma once

// Bring up WiFi and start the connect/poll state machine.
// Returns false if network support is not configured (mock mode).
bool net_init();

// Call every main-loop iteration (drives cyw43 polling + the state machine).
void net_task();

// Dispatch the land command (hooked into ui_panel's land handler).
// Returns false if the link is down / a post is already in flight.
bool net_send_land();
