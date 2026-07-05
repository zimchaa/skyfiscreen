#pragma once

// Starts the mock data feed (LVGL timers must be available, i.e. after
// lvgl_port_init). Phase 3 replaces this with the REST client.
void mock_data_start();
