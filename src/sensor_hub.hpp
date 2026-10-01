// Weather sensors on the Qw/ST bus (I2C0, GP40/41), all on the Enviro Weather:
//   BME280  @ 0x77  temperature / humidity / pressure   (read directly)
//   LTR-559 @ 0x23  light                               (read directly)
//   Enviro hub @ 0x42  wind / gust / direction / rain   (../common/enviro_hub_regs.h)
//
// Samples at 1 Hz, updates the dashboard tiles, and forwards a `wx` message to
// the Pi over pilink. A missing sensor shows "--" (never mock values) and is
// re-probed every few seconds, so the Enviro can be hot-plugged.
#pragma once

void sensor_hub_init();
void sensor_hub_task();   // call every main-loop iteration
