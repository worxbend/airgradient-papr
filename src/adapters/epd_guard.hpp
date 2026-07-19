// src/adapters/epd_guard.hpp
// The single choke point for every panel write (PLAN.md §6.5). Owns the
// 4 bpp framebuffer, power discipline, the partial-refresh budget + time
// backstop, the temperature clamp, boot-hygiene and the repair routine.
#pragma once
#include <Arduino.h>

#include "epd_driver.h"

namespace adapters {

class EpdGuard {
 public:
  static constexpr int W = EPD_WIDTH;                            // 960
  static constexpr int H = EPD_HEIGHT;                           // 540
  static constexpr int kPartialBudget = 6;                       // §6.1 N=6
  static constexpr uint32_t kFullBackstopMs = 15UL * 60 * 1000;  // §6.1

  // Refuse a panel push below this much *internal* free heap. epd_draw_*
  // spawns two 8 KB FreeRTOS worker tasks per frame; if that xTaskCreate fails
  // under low heap the draw blocks forever on a semaphore (see handbook §3 and
  // DECISIONS "white-screen hang"). Keep the region dirty and retry later.
  static constexpr uint32_t kMinHeapForRefresh = 55000u;

  // Panel-safe ambient range. The GC16 waveform is tuned for ~0..50 °C; driving
  // it well outside that risks a bad image or DC imbalance, so we hold the last
  // frame instead (costs nothing on e-paper) until temperature is back in band.
  static constexpr float kMinSafeTempC = 0.0f;
  static constexpr float kMaxSafeTempC = 50.0f;

  // epd_init + allocate the 4 bpp framebuffer (white) + boot hygiene.
  // Returns false if PSRAM allocation fails.
  //
  // clearOnBoot=true (default, USB profile): wipe the panel to white now.
  // clearOnBoot=false (battery timer wake): keep whatever image is already on
  // the bistable panel — the caller decides whether to redraw — so a skipped
  // refresh actually preserves pixels. Boot hygiene still forces a clear if the
  // previous run did not flag a clean shutdown, regardless of this flag.
  bool init(bool clearOnBoot = true);

  uint8_t* framebuffer() { return fb_; }

  // Accumulate a dirty rectangle (device pixel coords) from the flush_cb.
  void markDirty(int x, int y, int w, int h);

  // Push the accumulated dirty region to the panel; chooses partial vs a full
  // GC16 clear+redraw per the budget/backstop/area rules. forceFull overrides.
  void present(bool forceFull = false);

  void setAmbientTempC(float c) { tempC_ = c; }

  void repairRoutine(int cycles = 4);  // §6.2 alternating black/white
  void clearWhite();                   // full white GC16 frame
  void powerOffForSleep();             // epd_poweroff_all before sleep (§6.1.3)
  void markCleanShutdown();            // set NVS clean flag

  uint32_t fullRefreshes() const { return fullRefreshes_; }
  uint32_t partialRefreshes() const { return partialRefreshes_; }

 private:
  void fullRefresh();
  void partialRefresh();

  uint8_t* fb_ = nullptr;       // full screen, W/2 * H bytes
  uint8_t* scratch_ = nullptr;  // extraction buffer for partial pushes
  bool dirty_ = false;
  int dx0_ = 0, dy0_ = 0, dx1_ = 0, dy1_ = 0;  // union rect [x0,x1)
  int partialCount_ = 0;
  uint32_t lastFullMs_ = 0;
  float tempC_ = NAN;
  uint32_t fullRefreshes_ = 0;  // lifetime counters (for /health)
  uint32_t partialRefreshes_ = 0;
};

}  // namespace adapters
