// USB serial link to the SkyFi ground station (the Pi), protocol v1.
// Contract: skyfi-app/contracts/presto-link.md — one JSON object per line.
//
// Runs over the same USB CDC as printf debug output; the Pi ignores any line
// not starting with '{', so existing printf logging can stay.
#pragma once

#include <cstdint>

// Weather sample for pilink_send_weather(). NaN = field not available.
struct PiWeather {
    float wind, gust, dir, rain, rain_rate, temp, hum, pres, lux;
};
PiWeather pi_weather_empty();   // all fields NaN

// Last status pushed by the Pi.
struct PiStatus {
    char sys[12];     // ok | degraded | fault
    char drone[12];   // grounded | ascending | airborne | descending
    int  batt;        // %
    float alt;        // m
    float tgt;        // target altitude, m
    float tether;     // kg
    char power[10];   // tether | battery
    char autoland[8]; // armed | off
    char ip[16];
    char host[24];
    char msg[44];
    char clr[28];     // pre-flight clearance, e.g. "CLEARED until 09:21"
};

void pilink_init();

// Call every main-loop iteration: reads host lines, sends hello on (re)connect,
// retries an unacked land.
void pilink_task();

// True if a status arrived from the Pi within the last 5 s.
bool pilink_online();
const PiStatus& pilink_status();

void pilink_send_weather(const PiWeather& w);

// Land over USB; the Pi's ack arrives via ui_panel_land_result(). Retried
// with the same id until acked (the Pi de-duplicates). False if offline.
bool pilink_send_land();
