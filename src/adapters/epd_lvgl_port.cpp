// src/adapters/epd_lvgl_port.cpp
#include "adapters/epd_lvgl_port.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace adapters {

// ~1/10 screen partial render buffers (PLAN.md §D2): 960 x 54 px, L8 = 1 B/px.
static constexpr int kBufLines = 54;
static constexpr size_t kBufBytes = (size_t)EpdGuard::W * kBufLines;

static uint32_t tickCb() { return (uint32_t)millis(); }

static void flushCb(lv_display_t* disp, const lv_area_t* area,
                    uint8_t* px_map) {
  auto* guard = static_cast<EpdGuard*>(lv_display_get_user_data(disp));
  uint8_t* fb = guard->framebuffer();

  const int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
  const int aw = x2 - x1 + 1;

  // px_map is L8 (0 = black .. 255 = white), row stride == aw (stride align 1).
  for (int y = y1; y <= y2; ++y) {
    const uint8_t* src = px_map + (size_t)(y - y1) * aw;
    uint8_t* rowBase = fb + (size_t)y * (EpdGuard::W / 2);
    for (int x = x1; x <= x2; ++x) {
      const uint8_t l = src[x - x1];  // luminance, reused as 8-bit gray
      uint8_t* p = rowBase + (x >> 1);
      if (x & 1)
        *p = (*p & 0x0F) | (l & 0xF0);
      else
        *p = (*p & 0xF0) | (l >> 4);
    }
  }

  guard->markDirty(x1, y1, aw, y2 - y1 + 1);

  // Only push to the panel once LVGL has finished this whole refresh.
  if (lv_display_flush_is_last(disp)) guard->present(false);

  lv_display_flush_ready(disp);
}

lv_display_t* epdLvglInit(EpdGuard* guard) {
  lv_init();
  lv_tick_set_cb(tickCb);

  lv_display_t* disp = lv_display_create(EpdGuard::W, EpdGuard::H);
  if (!disp) return nullptr;
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);

  uint8_t* buf1 = (uint8_t*)heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM);
  uint8_t* buf2 = (uint8_t*)heap_caps_malloc(kBufBytes, MALLOC_CAP_SPIRAM);
  if (!buf1 || !buf2) return nullptr;

  lv_display_set_buffers(disp, buf1, buf2, kBufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_user_data(disp, guard);
  return disp;
}

}  // namespace adapters
