// LVGL heap pool allocator for PRESTO_FULL_RES builds (see lv_conf.h).
// C-compatible: included by LVGL's lv_mem_core_builtin.c.
#ifndef LVGL_PSRAM_POOL_H
#define LVGL_PSRAM_POOL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns a PSRAM block of at least `size` bytes for LVGL's built-in
// allocator. Defined in lvgl_port.cpp.
void *lvgl_psram_pool(size_t size);

#ifdef __cplusplus
}
#endif

#endif
