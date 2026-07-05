// SkyFi panel theme — colours from skyfiapp/prototype/design-tokens.css
// (Vodafone brand palette + Sky-Fi signal colours), adapted to a dark
// always-on panel.
#pragma once

#include "lvgl.h"

namespace theme {

// Brand
inline lv_color_t red()        { return lv_color_hex(0xE60000); }  // --vf-red
inline lv_color_t red_dark()   { return lv_color_hex(0xB30000); }  // --vf-red-dark
inline lv_color_t aqua()       { return lv_color_hex(0x00B0CA); }  // --sf-sky-blue
inline lv_color_t teal()       { return lv_color_hex(0x007C92); }  // --sf-sky-dark

// Signal / severity
inline lv_color_t ok()         { return lv_color_hex(0xA8B400); }  // --sf-signal-green
inline lv_color_t warn()       { return lv_color_hex(0xEB9800); }  // --sf-signal-amber
inline lv_color_t danger()     { return lv_color_hex(0xE60000); }  // --sf-signal-red

// Dark panel neutrals
inline lv_color_t bg()         { return lv_color_hex(0x141618); }
inline lv_color_t surface()    { return lv_color_hex(0x24282C); }  // tiles/cards
inline lv_color_t surface_hi() { return lv_color_hex(0x31363B); }
inline lv_color_t text()       { return lv_color_hex(0xFFFFFF); }
inline lv_color_t text_muted() { return lv_color_hex(0xBEBEBE); }  // --vf-light-grey
inline lv_color_t text_dim()   { return lv_color_hex(0x767676); }  // --vf-grey

enum class Sev { OK, WARN, DANGER };

inline lv_color_t sev_color(Sev s) {
    switch (s) {
        case Sev::WARN:   return warn();
        case Sev::DANGER: return danger();
        default:          return ok();
    }
}

}  // namespace theme
