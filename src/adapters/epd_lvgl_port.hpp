// src/adapters/epd_lvgl_port.hpp
// Wires LVGL 9 to the e-paper: L8 render buffers in PSRAM + a flush_cb that
// converts L8 -> 4 bpp into the EpdGuard framebuffer (PLAN.md §D2).
#pragma once
#include <lvgl.h>

#include "adapters/epd_guard.hpp"

namespace adapters {

// Creates the LVGL display bound to `guard`. Call lv_init()-dependent setup
// once at boot (from the UI task). Returns the display, or nullptr on OOM.
lv_display_t* epdLvglInit(EpdGuard* guard);

}  // namespace adapters
