// src/adapters/epd_lvgl_port.cpp
#include "adapters/epd_lvgl_port.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>

namespace adapters {

// ~1/10 screen partial render buffers: 960 x 54 px, L8 = 1 B/px.
static constexpr int kBufLines = 54;
static constexpr size_t kBufBytes = (size_t)EpdGuard::W * kBufLines;

static uint32_t tickCb() {
  return (uint32_t)millis();
}

static void flushCb(lv_display_t* disp, const lv_area_t* area,
                    uint8_t* px_map) {
  auto* guard = static_cast<EpdGuard*>(lv_display_get_user_data(disp));
  uint8_t* fb = guard ? guard->framebuffer() : nullptr;
  if (!fb) {  // bring-up failed; never write through a null framebuffer
    lv_display_flush_ready(disp);
    return;
  }

  const int x1 = area->x1, y1 = area->y1, x2 = area->x2, y2 = area->y2;
  const int aw = x2 - x1 + 1;

  // px_map is L8 (0 = black .. 255 = white), row stride == aw (stride align 1).
  //
  // The bulk of each row is packed two source pixels -> one destination byte
  // with a plain write. The per-pixel read-modify-write fallback runs only on
  // the (at most two) unaligned edge pixels: both buffers live in PSRAM, and
  // on the WROVER-E every PSRAM access pays the cache-errata barriers, so a
  // full-screen flush doing 518,400 RMWs was 50-150 ms of pure memory stalls.
  for (int y = y1; y <= y2; ++y) {
    const uint8_t* src = px_map + (size_t)(y - y1) * aw;
    uint8_t* rowBase = fb + (size_t)y * (EpdGuard::W / 2);
    int x = x1;
    if (x & 1) {  // odd leading pixel: high nibble of its byte
      uint8_t* p = rowBase + (x >> 1);
      *p = (*p & 0x0F) | (src[x - x1] & 0xF0);
      ++x;
    }
    for (; x + 1 <= x2; x += 2) {  // aligned pairs: single write, no read
      const uint8_t* s = src + (x - x1);
      rowBase[x >> 1] = (uint8_t)((s[0] >> 4) | (s[1] & 0xF0));
    }
    if (x == x2) {  // even trailing pixel: low nibble of its byte
      uint8_t* p = rowBase + (x >> 1);
      *p = (*p & 0xF0) | (src[x - x1] >> 4);
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
  if (!buf1 || !buf2) {
    heap_caps_free(buf1);
    heap_caps_free(buf2);
    lv_display_delete(disp);  // don't leave a half-built display registered
    return nullptr;
  }

  lv_display_set_buffers(disp, buf1, buf2, kBufBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_user_data(disp, guard);
  return disp;
}

}  // namespace adapters
