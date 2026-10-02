// Built instead of net.cpp + http_post.cpp when no WiFi credentials are
// configured: keeps the CYW43 driver and lwIP (and their ~45KB of SRAM) out
// of full-resolution builds, where the scanout buffer leaves little SRAM.
#include "net.hpp"
#include <cstdio>

bool net_init() {
    printf("net: WiFi fallback not configured (build with SKYFI_WIFI_SSID/SKYFI_API_HOST)\n");
    return false;
}
void net_task() {}
bool net_send_land() { return false; }
