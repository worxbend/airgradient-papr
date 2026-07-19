// src/adapters/epd_guard.cpp
#include "adapters/epd_guard.hpp"

#include <Preferences.h>
#include <esp_heap_caps.h>

namespace adapters {

static constexpr size_t kFbBytes =
    (size_t)EPD_WIDTH / 2 * EPD_HEIGHT;  // 259200

bool EpdGuard::init(bool clearOnBoot) {
  epd_init();

  fb_ = (uint8_t*)heap_caps_malloc(kFbBytes, MALLOC_CAP_SPIRAM);
  scratch_ = (uint8_t*)heap_caps_malloc(kFbBytes, MALLOC_CAP_SPIRAM);
  if (!fb_ || !scratch_) return false;
  memset(fb_, 0xFF, kFbBytes);  // 0xF nibbles = white

  // Boot hygiene (§6.1.4): if the previous run didn't flag a clean shutdown,
  // a crash/power-pull happened mid-life — clear the panel before first draw.
  Preferences prefs;
  prefs.begin("epd", false);
  bool wasClean = prefs.getBool("clean", true);
  prefs.putBool("clean", false);  // mark "running / dirty" for this session
  prefs.end();

  // A battery timer-wake with a clean shutdown keeps the existing image (a full
  // clear here would both waste a waveform and destroy the pixels a skipped
  // refresh means to preserve). Every other case starts from a clean panel.
  if (clearOnBoot || !wasClean) {
    epd_poweron();
    epd_clear();
    if (!wasClean) {
      // extra white settle frame after an unclean shutdown
      epd_draw_grayscale_image(epd_full_screen(), fb_);
    }
    epd_poweroff();
  }

  lastFullMs_ = millis();
  return true;
}

void EpdGuard::markDirty(int x, int y, int w, int h) {
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > W) x1 = W;
  if (y1 > H) y1 = H;
  if (x1 <= x || y1 <= y) return;
  if (!dirty_) {
    dx0_ = x;
    dy0_ = y;
    dx1_ = x1;
    dy1_ = y1;
    dirty_ = true;
  } else {
    if (x < dx0_) dx0_ = x;
    if (y < dy0_) dy0_ = y;
    if (x1 > dx1_) dx1_ = x1;
    if (y1 > dy1_) dy1_ = y1;
  }
}

void EpdGuard::present(bool forceFull) {
  if (!dirty_ && !forceFull) return;

  // Temperature clamp (§6.1.5): refuse refreshes outside the panel's safe
  // range; keep the last image (costs nothing) and retry when back in range.
  if (!isnan(tempC_) && (tempC_ < kMinSafeTempC || tempC_ > kMaxSafeTempC))
    return;

  // Heap gate: epd_draw_* spawns 8 KB worker tasks per frame; if internal heap
  // is momentarily low (a TLS/HTTP fetch holding a big buffer) that xTaskCreate
  // fails and hangs on a semaphore. Defer — keep dirty_ set and retry when heap
  // recovers (the interval refresh calls again shortly).
  if (ESP.getFreeHeap() < kMinHeapForRefresh) return;

  // Full refresh for every update (partial updates are disabled by request):
  // cleaner on this panel and avoids partial-refresh artifacts.
  epd_poweron();  // high-voltage rails ON only during the update (§6.1.2)
  fullRefresh();
  epd_poweroff();  // rails OFF immediately; panel holds the image with no power

  dirty_ = false;
}

void EpdGuard::fullRefresh() {
  epd_clear();
  epd_draw_grayscale_image(epd_full_screen(), fb_);
  partialCount_ = 0;
  lastFullMs_ = millis();
  fullRefreshes_++;
}

void EpdGuard::partialRefresh() {
  // Snap x to even boundaries so 4 bpp nibble packing stays byte-aligned.
  int x0 = dx0_ & ~1;
  int x1 = (dx1_ + 1) & ~1;
  if (x1 > W) x1 = W;
  int y0 = dy0_, y1 = dy1_;
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return;

  const int srcStride = W / 2;
  const int dstStride = w / 2;
  for (int row = 0; row < h; ++row) {
    memcpy(scratch_ + (size_t)row * dstStride,
           fb_ + (size_t)(y0 + row) * srcStride + x0 / 2, dstStride);
  }
  Rect_t area = {x0, y0, w, h};
  epd_clear_area(area);  // wipe ghosting in this card, then redraw it
  epd_draw_grayscale_image(area, scratch_);
  partialCount_++;
  partialRefreshes_++;
}

void EpdGuard::clearWhite() {
  memset(fb_, 0xFF, kFbBytes);
  epd_poweron();
  fullRefresh();
  epd_poweroff();
}

void EpdGuard::repairRoutine(int cycles) {
  // §6.2: alternate full black/white GC16 frames to shift stuck particles.
  epd_poweron();
  for (int i = 0; i < cycles; ++i) {
    epd_clear();
    memset(scratch_, 0x00, kFbBytes);  // black
    epd_draw_grayscale_image(epd_full_screen(), scratch_);
    delay(200);
    epd_clear();
    memset(scratch_, 0xFF, kFbBytes);  // white
    epd_draw_grayscale_image(epd_full_screen(), scratch_);
    delay(200);
  }
  epd_poweroff();
  partialCount_ = 0;
  lastFullMs_ = millis();
}

void EpdGuard::powerOffForSleep() {
  epd_poweroff_all();  // rails + peripherals down (§6.1.3) — battery + panel
                       // fix
}

void EpdGuard::markCleanShutdown() {
  Preferences prefs;
  prefs.begin("epd", false);
  prefs.putBool("clean", true);
  prefs.end();
}

}  // namespace adapters
