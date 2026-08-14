// src/adapters/epd_guard.cpp
#include "adapters/epd_guard.hpp"

#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

namespace adapters {

static constexpr size_t kFbBytes =
    (size_t)EPD_WIDTH / 2 * EPD_HEIGHT;  // 259200

// Set for the whole time the rails are up and the driver's worker tasks run.
// Read (never written) by the net task — a plain volatile bool is enough for a
// one-writer/one-reader flag on ESP32.
static volatile bool g_panelBusy = false;

bool panelBusy() {
  return g_panelBusy;
}

// Largest single internal (DRAM) block. This — not total free heap — is what
// `xTaskCreate` needs for the driver's 8 KB per-frame worker stacks.
static uint32_t largestInternalBlock() {
  return (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                    MALLOC_CAP_8BIT);
}

bool EpdGuard::init(bool clearOnBoot) {
  epd_init();

  fb_ = (uint8_t*)heap_caps_malloc(kFbBytes, MALLOC_CAP_SPIRAM);
  if (!fb_) return false;
  memset(fb_, 0xFF, kFbBytes);  // 0xF nibbles = white

  // Boot hygiene: if the previous run didn't flag a clean shutdown,
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
    g_panelBusy = true;
    epd_poweron();
    epd_clear();
    if (!wasClean) {
      // extra white settle frame after an unclean shutdown
      epd_draw_grayscale_image(epd_full_screen(), fb_);
    }
    epd_poweroff();
    g_panelBusy = false;
  }

  return true;
}

void EpdGuard::setRefreshLimits(uint32_t minMs, uint32_t forcedMinMs) {
  minIntervalMs_ = minMs;
  forcedMinIntervalMs_ = forcedMinMs;
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

bool EpdGuard::rateLimitAllows() const {
  if (!havePresented_) return true;  // never delay the very first frame
  const uint32_t need = pendingForce_ ? forcedMinIntervalMs_ : minIntervalMs_;
  // Unsigned subtraction — correct across the 49.7-day millis() wrap.
  return (uint32_t)(millis() - lastPresentMs_) >= need;
}

uint32_t EpdGuard::msUntilAllowed() const {
  if (!havePresented_) return 0;
  const uint32_t need = pendingForce_ ? forcedMinIntervalMs_ : minIntervalMs_;
  const uint32_t since = (uint32_t)(millis() - lastPresentMs_);
  return since >= need ? 0 : need - since;
}

void EpdGuard::setAmbientTempC(float c) {
  tempC_ = c;
  tempSetMs_ = millis();
}

// True when the ambient proxy says it is unsafe to drive a waveform. A reading
// older than kTempStaleMs is treated as no reading at all — see the header.
bool EpdGuard::tempClampBlocks() {
  const bool fresh =
      !isnan(tempC_) && (uint32_t)(millis() - tempSetMs_) < kTempStaleMs;
  const bool blocks =
      fresh && (tempC_ < kMinSafeTempC || tempC_ > kMaxSafeTempC);
  if (blocks != tempBlocking_) {  // edge only — this would spam every loop pass
    tempBlocking_ = blocks;
    if (blocks)
      Serial.printf("[epd] temperature clamp ON (%.1fC outside %.0f..%.0f) — "
                    "holding the last image\n",
                    tempC_, kMinSafeTempC, kMaxSafeTempC);
    else
      Serial.println("[epd] temperature clamp OFF — refreshing again");
  }
  return blocks;
}

void EpdGuard::present(bool forceFull) {
  if (forceFull) pendingForce_ = true;
  if (!dirty_ && !pendingForce_) return;

  // Rate limit (the once-per-minute rule). The framebuffer already holds the
  // newest image and `dirty_` stays set, so tick() re-enters here and pushes
  // the *latest* content the moment the interval expires — deferring costs
  // freshness, never content.
  if (!rateLimitAllows()) {
    deferredRate_++;
    return;
  }

  // Temperature clamp: refuse refreshes outside the panel's safe
  // range; keep the last image (costs nothing) and retry when back in range.
  if (tempClampBlocks()) {
    deferredTemp_++;
    return;
  }

  // Heap gate: epd_draw_* spawns 8 KB worker tasks per frame; if internal heap
  // is momentarily low (a TLS/HTTP fetch holding a big buffer) that xTaskCreate
  // fails and hangs on a semaphore. Both totals matter — enough free heap AND
  // one contiguous block big enough for the stacks. Defer — keep dirty_ set and
  // retry when heap recovers (tick() calls again shortly).
  if (ESP.getFreeHeap() < kMinHeapForRefresh ||
      largestInternalBlock() < kMinContiguousForRefresh) {
    deferredHeap_++;
    return;
  }

  // Full refresh for every update (partial updates are disabled by request):
  // cleaner on this panel and avoids partial-refresh artifacts.
  const uint32_t t0 = millis();
  g_panelBusy = true;  // net task holds off any big allocation until we're done
  epd_poweron();       // high-voltage rails ON only during the update
  fullRefresh();
  epd_poweroff();  // rails OFF immediately; panel holds the image with no power
  g_panelBusy = false;

  lastRefreshDurMs_ = (uint32_t)(millis() - t0);
  if (lastRefreshDurMs_ > maxRefreshDurMs_) maxRefreshDurMs_ = lastRefreshDurMs_;

  lastPresentMs_ = millis();
  havePresented_ = true;
  pendingForce_ = false;
  dirty_ = false;
}

void EpdGuard::fullRefresh() {
  epd_clear();
  epd_draw_grayscale_image(epd_full_screen(), fb_);
  fullRefreshes_++;
}

void EpdGuard::clearWhite() {
  memset(fb_, 0xFF, kFbBytes);
  g_panelBusy = true;
  epd_poweron();
  fullRefresh();
  epd_poweroff();
  g_panelBusy = false;
  lastPresentMs_ = millis();
  havePresented_ = true;
}

bool EpdGuard::repairRoutine(int cycles) {
  // Alternate full black/white GC16 frames to shift stuck particles.
  //
  // The solid-colour source buffer is allocated here and released immediately:
  // it is a quarter-megabyte that used to sit in PSRAM for the whole uptime for
  // the sake of a routine that runs seconds per year.
  uint8_t* solid = (uint8_t*)heap_caps_malloc(kFbBytes, MALLOC_CAP_SPIRAM);
  if (!solid) return false;

  g_panelBusy = true;
  epd_poweron();
  for (int i = 0; i < cycles; ++i) {
    esp_task_wdt_reset();  // ~2.5 s per cycle; keep the UI task's WDT fed
    epd_clear();
    memset(solid, 0x00, kFbBytes);  // black
    epd_draw_grayscale_image(epd_full_screen(), solid);
    delay(200);
    epd_clear();
    memset(solid, 0xFF, kFbBytes);  // white
    epd_draw_grayscale_image(epd_full_screen(), solid);
    delay(200);
  }
  epd_poweroff();
  g_panelBusy = false;
  heap_caps_free(solid);

  // The panel now holds white, not our framebuffer — force a repaint. Note we
  // deliberately do NOT stamp lastPresentMs_: it still points at the push before
  // the repair, which is already older than the forced floor, so the next tick()
  // repaints immediately instead of leaving the user staring at a white screen.
  dirty_ = true;
  pendingForce_ = true;
  return true;
}

void EpdGuard::powerOffForSleep() {
  // epd_poweroff_all(), not epd_poweroff(): the plain call drops only the
  // high-voltage rails, leaving the panel's peripheral power domain energised —
  // which both drains the battery across deep sleep and can leave the panel in
  // a partially powered state. The _all variant shuts down everything.
  epd_poweroff_all();
  g_panelBusy = false;
}

void EpdGuard::markCleanShutdown() {
  Preferences prefs;
  prefs.begin("epd", false);
  prefs.putBool("clean", true);
  prefs.end();
}

}  // namespace adapters
