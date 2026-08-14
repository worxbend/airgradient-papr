// src/adapters/epd_guard.hpp
// The single choke point for every panel write. Owns the 4 bpp framebuffer,
// power discipline, the refresh RATE LIMIT, the heap gates, the temperature
// clamp, boot-hygiene and the repair routine.
//
// Rate limit (handbook ch. 03, "How often may we refresh?"): one full GC16
// update costs ~47 panel frames (`epd_clear()` = 32, `epd_draw_grayscale_image()`
// = 15), blocks the calling task for ~1 s and peaks the panel at up to 950 mW
// (ED047TC1 datasheet §6-2). Pushing one every 30 s — which the unconditional
// interval redraw used to do — is ~2 880 full-screen waveforms a day for a
// clock that only changes once a minute. `present()` therefore refuses to touch
// the panel more often than `minIntervalMs_` (default 60 s), keeps the region
// dirty, and pushes the *latest* framebuffer at the next allowed slot. Explicit
// user actions (a page switch) take the shorter `forcedMinIntervalMs_` path so
// button presses still feel instant without letting a mashed button burn a
// waveform per press.
#pragma once
#include <Arduino.h>

#include "epd_driver.h"

namespace adapters {

// True while the high-voltage rails are up and the driver's per-frame worker
// tasks are running. The net task polls this and defers starting an HTTP/TLS
// fetch, so a 40 KB mbedTLS allocation can never land in the middle of a draw
// and starve `epd_draw_image`'s two 8 KB `xTaskCreate`s (that failure mode is a
// permanent deadlock — see kMinContiguousForRefresh below).
bool panelBusy();

class EpdGuard {
 public:
  static constexpr int W = EPD_WIDTH;   // 960
  static constexpr int H = EPD_HEIGHT;  // 540

  // Refuse a panel push below this much *internal* free heap. epd_draw_*
  // spawns two 8 KB FreeRTOS worker tasks per frame; if that xTaskCreate fails
  // under low heap the draw blocks forever on a semaphore (see handbook ch. 03
  // and DECISIONS "white-screen hang"). Keep the region dirty and retry later.
  //
  // 35 KB, not more: the real requirement is the CONTIGUOUS check below (two
  // 8 KB stacks + TCBs); this total-free check is only a sanity margin. It
  // used to be 55000, which steady-state internal DRAM can drift under with
  // zero fragmentation (the driver alone pins a 64 KB internal LUT; Wi-Fi +
  // WebServer + OTA + mDNS take the rest) — closing the gate permanently and
  // freezing the screen while everything else keeps running.
  static constexpr uint32_t kMinHeapForRefresh = 35000u;

  // ...and total free heap is not enough on its own: a task stack must come out
  // of ONE contiguous internal block. After days of `String`/mbedTLS churn the
  // heap fragments, so free-heap can read 60 KB while the largest block is a
  // few KB — `xTaskCreate` then fails and `epd_draw_image` deadlocks on
  // `xSemaphoreTake(..., portMAX_DELAY)` with the rails still on. Require room
  // for both 8 KB stacks plus their TCBs and some slack.
  static constexpr uint32_t kMinContiguousForRefresh = 20000u;

  // Panel-safe ambient range. The GC16 waveform is tuned for ~0..50 °C; driving
  // it well outside that risks a bad image or DC imbalance, so we hold the last
  // frame instead (costs nothing on e-paper) until temperature is back in band.
  // Matches the ED047TC1 datasheet operating range (0..+50 °C).
  //
  // CAVEAT: the reading is the AirGradient's *indoor air* temperature, used as a
  // proxy for panel temperature. That only holds if the sensor and the panel are
  // in the same room — which is the intended deployment, but it is an assumption,
  // not a measurement. There is no panel thermistor on this board and this driver
  // fork exposes no waveform temperature-set API (DECISIONS #3).
  static constexpr float kMinSafeTempC = 0.0f;
  static constexpr float kMaxSafeTempC = 50.0f;

  // ...and a proxy that stops arriving must not clamp forever. If no fresh
  // reading lands within this window the temperature is treated as unknown and
  // the clamp opens: a wrong-but-plausible ambient guess is a smaller risk than
  // a screen that is frozen for good because the sensor went offline in winter.
  static constexpr uint32_t kTempStaleMs = 15UL * 60 * 1000;

  // epd_init + allocate the 4 bpp framebuffer (white) + boot hygiene.
  // Returns false if PSRAM allocation fails.
  //
  // clearOnBoot=true (default, USB profile): wipe the panel to white now.
  // clearOnBoot=false (battery timer wake): keep whatever image is already on
  // the bistable panel — the caller decides whether to redraw — so a skipped
  // refresh actually preserves pixels. Boot hygiene still forces a clear if the
  // previous run did not flag a clean shutdown, regardless of this flag.
  //
  // nvsHygiene=false skips the NVS clean-flag dance entirely (no flash writes;
  // the previous shutdown is assumed clean). The battery profile uses this and
  // tracks cleanliness in RTC slow memory instead: two NVS commits per 15-min
  // wake would be ~192 flash writes a day for a flag deep sleep preserves for
  // free — see markCleanShutdown().
  bool init(bool clearOnBoot = true, bool nvsHygiene = true);

  uint8_t* framebuffer() { return fb_; }

  // Minimum wall-clock gap between two panel updates. `minMs` governs ordinary
  // data-driven redraws (the once-per-minute rule); `forcedMinMs` governs
  // explicit user actions requested via requestForce(). Call once at boot.
  void setRefreshLimits(uint32_t minMs, uint32_t forcedMinMs);

  // Accumulate a dirty rectangle (device pixel coords) from the flush_cb.
  void markDirty(int x, int y, int w, int h);

  // Push the accumulated dirty region to the panel — subject to the rate limit,
  // the temperature clamp and the heap gates. If any gate blocks the push the
  // region stays dirty and tick() retries it later; nothing is ever lost, it is
  // only delayed. forceFull additionally sets the pending-force flag.
  void present(bool forceFull = false);

  // Mark the next push as user-initiated: it uses the short forced interval
  // instead of the once-per-minute floor. Coalesces — several requests inside
  // one interval still cost exactly one refresh.
  void requestForce() { pendingForce_ = true; }

  // Call every loop iteration. Retries a push that an earlier gate deferred.
  void tick() { present(false); }

  // Milliseconds until the rate limiter would allow the next push (0 = now).
  uint32_t msUntilAllowed() const;

  void setAmbientTempC(float c);

  // Alternating black/white GC16 frames to shift stuck particles. Blocking
  // (~2.5 s per cycle) and MUST be called from the UI task — it drives the panel
  // directly, so calling it from the net task would race a refresh in flight.
  // Returns false if its scratch buffer could not be allocated.
  bool repairRoutine(int cycles = 4);
  void clearWhite();        // full white GC16 frame
  void powerOffForSleep();  // epd_poweroff_all before sleep
  void markCleanShutdown();  // set NVS clean flag

  uint32_t fullRefreshes() const { return fullRefreshes_; }
  // Diagnostics for /health — why a push did not happen, and how long the last
  // one actually took on the panel.
  uint32_t deferredRateLimit() const { return deferredRate_; }
  uint32_t deferredLowHeap() const { return deferredHeap_; }
  uint32_t deferredTemp() const { return deferredTemp_; }
  // How long the heap gate has been continuously blocking a pending push
  // (0 = not currently starved). The composition root watches this and
  // reboots past a threshold: fragmentation heals with a reboot, never by
  // waiting — without this backstop a closed gate is a silent, permanent
  // freeze with Wi-Fi still up (the observed "static picture" failure).
  uint32_t heapStarvedMs() const {
    return heapBlocked_ ? (uint32_t)(millis() - heapBlockedSinceMs_) : 0;
  }
  uint32_t lastRefreshMs() const { return lastRefreshDurMs_; }
  uint32_t maxRefreshMs() const { return maxRefreshDurMs_; }

 private:
  bool rateLimitAllows() const;
  bool tempClampBlocks();
  void fullRefresh();

  uint8_t* fb_ = nullptr;  // full screen, W/2 * H bytes
  bool dirty_ = false;
  int dx0_ = 0, dy0_ = 0, dx1_ = 0, dy1_ = 0;  // union rect [x0,x1)
  float tempC_ = NAN;
  uint32_t tempSetMs_ = 0;
  bool tempBlocking_ = false;  // edge-detect, so the clamp logs once not forever
  bool heapBlocked_ = false;   // heap gate currently starving a pending push
  uint32_t heapBlockedSinceMs_ = 0;  // when the current starve streak began

  // ---- rate limiter -------------------------------------------------------
  uint32_t minIntervalMs_ = 60000;      // once-per-minute floor
  uint32_t forcedMinIntervalMs_ = 5000;  // floor for user-initiated pushes
  uint32_t lastPresentMs_ = 0;
  bool havePresented_ = false;  // the very first frame is never rate-limited
  bool pendingForce_ = false;

  uint32_t fullRefreshes_ = 0;  // lifetime counters (for /health)
  uint32_t deferredRate_ = 0;
  uint32_t deferredHeap_ = 0;
  uint32_t deferredTemp_ = 0;
  uint32_t lastRefreshDurMs_ = 0;
  uint32_t maxRefreshDurMs_ = 0;
};

}  // namespace adapters
