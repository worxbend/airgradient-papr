// src/main.cpp — composition root.
//
// This is the "front door": it creates every adapter and wires the layers
// together. New to the project? Read docs/handbook/ (ch. 01 architecture,
// ch. 02 power) alongside this file.
//
// Default (USB always-on): net_task (core 0) polls Wi-Fi/HTTP into a length-1
// mailbox; the Arduino loop (core 1) renders LVGL + drives the panel. All LVGL
// calls stay on the UI task.
//
// -DPROFILE_BATTERY: single-shot — wake, connect, poll, render only when the
// reading changed, power the panel fully off, deep-sleep for
// cfg::kBatteryPollSeconds. See docs/handbook/02-power-model.md.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <lvgl.h>
#include <time.h>

#include "adapters/airgradient_http.hpp"
#include "adapters/currency_http.hpp"
#include "adapters/epd_guard.hpp"
#include "adapters/epd_lvgl_port.hpp"
#include "adapters/weather_http.hpp"
#include "adapters/wifi_link.hpp"
#include "app/buttons.hpp"
#include "app/clock.hpp"
#include "app/health.hpp"
#include "app/snapshot.hpp"
#include "config.hpp"
#include "ui/ui.hpp"

#if !defined(PROFILE_BATTERY)
#include "adapters/net_services.hpp"
#endif
#if defined(PROFILE_BATTERY)
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "app/power.hpp"
#endif

static adapters::EpdGuard g_guard;
static lv_display_t* g_disp = nullptr;

// clearPanel=false preserves the on-panel image across a battery timer wake
// (see EpdGuard::init); the USB profile always clears.
//
// Each step is a hard prerequisite for the next, so a failure returns
// immediately instead of carrying on with a null framebuffer — LVGL would then
// flush straight through a null pointer on the first render.
static bool bringUpDisplay(bool clearPanel = true, bool nvsHygiene = true) {
  if (!psramFound()) {
    Serial.println("[fatal] PSRAM not found — check board/build flags");
    return false;
  }
  Serial.printf("[boot] PSRAM: %u bytes free\n", (unsigned)ESP.getFreePsram());

  if (!g_guard.init(clearPanel, nvsHygiene)) {
    Serial.println("[fatal] EPD framebuffer alloc failed");
    return false;
  }
  g_guard.setRefreshLimits(cfg::kMinRefreshIntervalSeconds * 1000UL,
                           cfg::kForcedRefreshMinSeconds * 1000UL);

  g_disp = adapters::epdLvglInit(&g_guard);
  if (!g_disp) {
    Serial.println("[fatal] LVGL display init failed");
    return false;
  }
  ui::build();
  return true;
}

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "other-wdt";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "unknown";
  }
}

// ===========================================================================
#if !defined(PROFILE_BATTERY)  // ---- USB always-on profile ----------------
// ===========================================================================
static QueueHandle_t g_mailbox = nullptr;
static app::Buttons g_buttons;

// Hold off starting a fetch while the panel is mid-refresh. `epd_draw_image`
// spawns two 8 KB tasks per frame (15 frames); if a 40 KB mbedTLS handshake or
// a chunked-response String lands in that window, xTaskCreate fails and the
// driver blocks forever on `xSemaphoreTake(..., portMAX_DELAY)` with the rails
// still energised. A refresh takes ~1 s and now happens at most once a minute,
// so waiting for it costs nothing. Bounded so a stuck flag can't wedge the net
// task in turn.
static void waitForPanelIdle(uint32_t maxWaitMs = 5000) {
  uint32_t t0 = millis();
  while (adapters::panelBusy() && (uint32_t)(millis() - t0) < maxWaitMs)
    vTaskDelay(pdMS_TO_TICKS(20));
}

static void fetchWeather(adapters::WeatherHttp& wx, app::Snapshot& snap,
                         bool& geoDone, double& lat, double& lon) {
  if (!geoDone) {
    long off = 0;
    if (cfg::kLatitude != 0.0 || cfg::kLongitude != 0.0) {
      lat = cfg::kLatitude;
      lon = cfg::kLongitude;
      geoDone = true;
    } else if (wx.geolocate(lat, lon, snap.weather.city,
                            sizeof(snap.weather.city), off)) {
      geoDone = true;
      Serial.printf("[geo] %.3f, %.3f  %s\n", lat, lon, snap.weather.city);
    } else {
      Serial.printf("[geo] failed: %s\n", wx.lastError());
      return;
    }
  }
  domain::Weather w;
  if (wx.fetch(lat, lon, snap.weather.city, w)) {
    snap.weather = w;
    Serial.printf("[wx] %s %.0fC UV %.0f %s\n", w.city, w.tempC, w.uvIndex,
                  w.desc);
  } else {
    Serial.printf("[wx] failed: %s\n", wx.lastError());
  }
}

static void netTask(void*) {
  adapters::WifiLink wifi(cfg::kWifiSsid, cfg::kWifiPass);
  adapters::AirGradientHttp source(cfg::kMonitorUrl);
  adapters::WeatherHttp wx;
  adapters::CurrencyHttp currency;

  app::Snapshot snap;
  snap.state = app::NetState::Connecting;
  xQueueOverwrite(g_mailbox, &snap);

  wifi.begin();
  configTime(cfg::kGmtOffsetSec, cfg::kDstOffsetSec, "pool.ntp.org",
             "time.nist.gov");

  uint32_t lastPoll = 0, lastWeather = 0, lastCurrency = 0;
  bool geoDone = false;
  double lat = 0, lon = 0;
  uint32_t lastStackCheck = 0;
  for (;;) {
    app::g_health.netTickMs = millis();  // liveness beacon (watched by loop())

    // Stack low-water mark every ~10 s (the call scans the stack — too costly
    // for every 50 ms pass). ESP-IDF returns bytes, not FreeRTOS words.
    if ((uint32_t)(millis() - lastStackCheck) >= 10000) {
      lastStackCheck = millis();
      app::g_health.netStackMinFree =
          (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    }

    bool connected = wifi.ensureConnected();
    snap.rssi = wifi.rssi();
    app::g_health.rssi = snap.rssi;

    if (!connected) {
      snap.state =
          snap.m.valid ? app::NetState::Offline : app::NetState::Connecting;
      xQueueOverwrite(g_mailbox, &snap);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    adapters::netServicesBegin("airdeck");  // idempotent
    adapters::netServicesHandle();

    uint32_t nowW = millis();
    if (lastWeather == 0 ||
        nowW - lastWeather >= cfg::kWeatherPollSeconds * 1000UL) {
      lastWeather = nowW;
      waitForPanelIdle();
      fetchWeather(wx, snap, geoDone, lat, lon);
      xQueueOverwrite(g_mailbox, &snap);
    }

    uint32_t nowC = millis();
    bool timeReady = app::timeIsSynced();
    if ((lastCurrency == 0 && timeReady) ||
        (lastCurrency != 0 &&
         nowC - lastCurrency >= cfg::kCurrencyPollSeconds * 1000UL)) {
      lastCurrency = nowC;
      waitForPanelIdle();
      domain::Currency cc;
      if (currency.fetch(cc)) {
        snap.currency = cc;
        Serial.printf(
            "[cur] USD=%.2f EUR=%.2f CNY=%.4f BTC=%.0f ETH=%.0f hist=%d\n",
            cc.usdUah, cc.eurUah, cc.cnyUsd, cc.btcUsd, cc.ethUsd,
            cc.histCount);
      } else {
        Serial.printf("[cur] failed: %s\n", currency.lastError());
      }
      xQueueOverwrite(g_mailbox, &snap);
    }

    uint32_t now = millis();
    if (lastPoll == 0 || now - lastPoll >= cfg::kPollSeconds * 1000UL) {
      lastPoll = now;
      waitForPanelIdle();
      domain::Measurement m;
      if (source.poll(m)) {
        snap.m = m;
        snap.state = app::NetState::Online;
        snap.lastOkMs = millis();
        snap.consecutiveFail = 0;
        snap.pollCount++;
        app::g_health.pollCount = snap.pollCount;
        app::g_health.consecutiveFail = 0;
        app::g_health.lastOkMs = snap.lastOkMs;
        strlcpy(app::g_health.sensorFirmware, m.firmware,
                sizeof(app::g_health.sensorFirmware));
        Serial.printf("[net] ok #%lu  CO2=%.0f PM2.5=%.1f T=%.1f RH=%.0f\n",
                      (unsigned long)snap.pollCount, m.rco2, m.pm02, m.tempC(),
                      m.humidity());
      } else {
        snap.consecutiveFail++;
        app::g_health.consecutiveFail = snap.consecutiveFail;
        Serial.printf("[net] poll failed: %s (fail #%lu)\n", source.lastError(),
                      (unsigned long)snap.consecutiveFail);
        if (snap.m.valid) {
          uint32_t age = millis() - snap.lastOkMs;
          snap.state = (age > 3 * cfg::kPollSeconds * 1000UL)
                           ? app::NetState::Stale
                           : app::NetState::Online;
        } else {
          snap.state = app::NetState::Connecting;
        }
      }
      xQueueOverwrite(g_mailbox, &snap);
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // keep OTA/web responsive
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  const esp_reset_reason_t why = esp_reset_reason();
  app::g_health.resetReason = (uint32_t)why;
  Serial.printf("\n[airdeck] boot (USB profile) — last reset: %s\n",
                resetReasonName(why));

  if (!bringUpDisplay()) {
    // Nothing can be drawn and nothing can be fetched usefully. Park here
    // loudly rather than crash-looping through a null framebuffer; the task
    // watchdog is deliberately not armed yet, so this stays parked for
    // inspection instead of rebooting into the same failure forever.
    for (;;) {
      Serial.println("[fatal] display bring-up failed — halted");
      delay(10000);
    }
  }
  lv_timer_handler();  // render the (empty-state) main page now, while heap is
                       // high

  g_buttons.begin();
  g_mailbox = xQueueCreate(1, sizeof(app::Snapshot));
  app::g_health.netTickMs = millis();  // seed before the watcher can fire
  // 16 KB stack: mbedTLS (HTTPS weather fetch) needs the headroom.
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 4, nullptr, 0);

  // Hardware task watchdog on the UI task. The panel driver's per-frame
  // xTaskCreate can fail on a fragmented heap and then block forever on a
  // semaphore — historically the "needs a manual reset" freeze. With this
  // armed, that deadlock reboots the board in kWatchdogSeconds instead.
  esp_task_wdt_init(cfg::kWatchdogSeconds, /*panic=*/true);
  esp_task_wdt_add(nullptr);  // subscribe the Arduino loop task
  Serial.printf("[boot] heap free: %u  wdt: %lus\n", (unsigned)ESP.getFreeHeap(),
                (unsigned long)cfg::kWatchdogSeconds);
}

void loop() {
  static app::Snapshot snap;
  static uint32_t lastUiTick = 0;
  static uint32_t lastDeGhost = 0;

  esp_task_wdt_reset();

  app::Snapshot incoming;
  if (xQueueReceive(g_mailbox, &incoming, 0) == pdTRUE) {
    snap = incoming;
    if (snap.m.valid) g_guard.setAmbientTempC(snap.m.tempC());
    ui::update(snap);
  }

  // Recompute the page models (clock, "last seen", bands). Pure CPU — this does
  // NOT drive the panel. Labels that didn't change don't invalidate, so a tick
  // where nothing moved leaves the framebuffer clean and costs no refresh.
  if ((uint32_t)(millis() - lastUiTick) >= cfg::kUiTickSeconds * 1000UL) {
    lastUiTick = millis();
    ui::update(snap);
  }

  // De-ghost backstop: repaint the whole page periodically even if every label
  // is unchanged, to clear e-paper particle memory.
  if (cfg::kDeGhostRefreshSeconds &&
      (uint32_t)(millis() - lastDeGhost) >=
          cfg::kDeGhostRefreshSeconds * 1000UL) {
    lastDeGhost = millis();
    ui::refreshCurrent();
  }

  // Button navigation: left = prev page, mid = main (AQ), right = next.
  switch (g_buttons.poll()) {
    case app::Btn::Left:
      ui::prevPage();
      break;
    case app::Btn::Mid:
      ui::gotoMain();
      break;
    case app::Btn::Right:
      ui::nextPage();
      break;
    default:
      break;
  }

  // De-ghost on demand (GET /repair). The endpoint runs on the net task and only
  // raises a flag; the panel work happens here, on the task that owns it.
  if (adapters::netServicesTakeRepairRequest()) {
    Serial.println("[epd] de-ghost requested — running repair routine");
    if (!g_guard.repairRoutine()) Serial.println("[epd] repair: alloc failed");
    ui::refreshCurrent();  // repaint over the white the routine leaves behind
  }

  // A page switch is an explicit interaction — let it jump the once-per-minute
  // data-refresh queue (still floored by kForcedRefreshMinSeconds). Flag it
  // BEFORE the render so the flush_cb's present() already sees the priority.
  if (ui::takePageChanged()) g_guard.requestForce();

  lv_timer_handler();  // renders into the framebuffer; present() gates the push

  // Retry any push an earlier gate deferred (rate limit, heap, temperature).
  g_guard.tick();

  app::g_health.fullRefreshes = g_guard.fullRefreshes();
  app::g_health.refreshDeferredRate = g_guard.deferredRateLimit();
  app::g_health.refreshDeferredHeap = g_guard.deferredLowHeap();
  app::g_health.refreshDeferredTemp = g_guard.deferredTemp();
  app::g_health.lastRefreshMs = g_guard.lastRefreshMs();
  app::g_health.maxRefreshMs = g_guard.maxRefreshMs();
  app::g_health.refreshStarvedMs = g_guard.heapStarvedMs();

  // Refresh-starvation backstop, symmetric with the net-stall watchdog below.
  // The heap gate deferring is normal for seconds (a TLS handshake in flight);
  // deferring CONTINUOUSLY for minutes means internal DRAM is fragmented for
  // good, and only a reboot heals that. Without this, the failure mode is a
  // healthy-looking device showing a stale image forever.
  const uint32_t starved = g_guard.heapStarvedMs();
  if (starved > cfg::kRefreshStarveRebootSeconds * 1000UL) {
    Serial.printf("[fatal] refresh starved by heap gate for %lus — rebooting\n",
                  (unsigned long)(starved / 1000));
    Serial.flush();
    esp_restart();
  }

  // Net-task stall watchdog. The task WDT only covers this task; a socket or
  // TLS session wedged inside HTTPClient would otherwise leave a healthy-looking
  // but permanently stale screen with no way out but the reset button.
  const uint32_t netAge = (uint32_t)(millis() - app::g_health.netTickMs);
  if (netAge > cfg::kNetStallRebootSeconds * 1000UL) {
    Serial.printf("[fatal] net task stalled %lus — rebooting\n",
                  (unsigned long)(netAge / 1000));
    Serial.flush();
    esp_restart();
  }

  // 20 ms ≈ 50 Hz: the screen changes at most every 3 s and the debouncer is
  // happy at this rate; spinning at 200 Hz (the old delay(5)) only burned CPU.
  delay(20);
}

// ===========================================================================
#else  // ---- Battery deep-sleep profile (single-shot per wake) ------------
// ===========================================================================
//
// Runtime on a LiPo is dominated by how often we wake, so this profile:
//   * sleeps for cfg::kBatteryPollSeconds (minutes, not the 30 s USB cadence);
//   * preserves the bistable panel image across a timer wake and only spends a
//     full GC16 refresh when the air reading actually changed (or every Nth
//     wake, to refresh the clock and clear ghosting);
//   * never wastes a refresh on a "Connecting" splash — the last good frame
//     stays on screen for the few seconds it takes to poll.
// See docs/handbook/02-power-model.md for the numbers.

// RTC slow-memory state — survives deep sleep, lost on power-cut / reset.
RTC_DATA_ATTR static uint32_t rtcLastSig = 0;  // signature of on-panel reading
RTC_DATA_ATTR static uint32_t rtcWakeCount = 0;  // wakes since cold boot
RTC_DATA_ATTR static bool rtcHasImage = false;  // has the panel ever been drawn
// Clean-shutdown flag: set just before deep sleep, cleared at the top of every
// wake. Replaces the NVS "clean" flag on battery — two flash commits per
// 15-minute wake would be ~192 flash writes a day for state that RTC memory
// preserves for free (and a crash/power-pull wipes RTC, which reads as
// "unclean" exactly as intended).
RTC_DATA_ATTR static bool rtcCleanShutdown = false;
// Wi-Fi fast-reconnect hint (channel + BSSID + DHCP lease) captured after each
// successful connect; skips the all-channel scan + DHCP on the next wake.
RTC_DATA_ATTR static adapters::WifiFastHint rtcWifiHint;
// When SNTP last completed, so the (async, radio-chatty) sync can run daily
// instead of every wake — system time itself survives deep sleep on the RTC.
RTC_DATA_ATTR static time_t rtcLastNtpSync = 0;

// The TZ environment variable does NOT survive deep sleep, so on wakes that
// skip configTime() the timezone must still be restored or the on-screen
// clock would silently render UTC. POSIX TZ signs are inverted: UTC+3 is
// "UTC-3:00".
static void restoreTimezone() {
  const long total = cfg::kGmtOffsetSec + cfg::kDstOffsetSec;
  char tz[24];
  snprintf(tz, sizeof(tz), "UTC%+ld:%02ld", -(total / 3600),
           labs(total % 3600) / 60);
  setenv("TZ", tz, 1);
  tzset();
}

// FNV-1a hash of the values we actually render, quantised to display precision,
// so a sub-digit sensor wiggle doesn't cost a full-screen refresh.
static uint32_t measurementSignature(const domain::Measurement& m) {
  auto q = [](float v, float scale) -> int32_t {
    return domain::has(v) ? (int32_t)lroundf(v * scale) : INT32_MIN;
  };
  const int32_t fields[] = {
      q(m.rco2, 1),       q(m.pm02, 10),     q(m.tempC(), 1),
      q(m.humidity(), 1), q(m.tvocIndex, 1), q(m.noxIndex, 1),
      q(m.pm01, 10),      q(m.pm10, 10),     q(m.pm003Count, 1),
  };
  uint32_t h = 2166136261u;
  for (int32_t v : fields) {
    const uint8_t* p = (const uint8_t*)&v;
    for (int i = 0; i < 4; ++i) {
      h ^= p[i];
      h *= 16777619u;
    }
  }
  return h ? h : 1u;  // reserve 0 for "no signature yet"
}

// displayUp: whether bringUpDisplay() ran this wake. A skip-refresh wake never
// initialises the panel, and epd_poweroff_all() on an uninitialised driver
// would poke unconfigured rails.
static void sleepFor(uint32_t seconds, bool displayUp) {
  rtcCleanShutdown = true;  // clean flag -> next wake may preserve the image
  if (displayUp) g_guard.powerOffForSleep();  // rails + peripherals down
  WiFi.disconnect(true, false);
  Serial.printf("[sleep] %lus\n", (unsigned long)seconds);
  Serial.flush();
  // WROVER modules pull GPIO12 (MTDI strapping) up externally while the chip
  // applies an internal pulldown — that divider leaks for the whole deep sleep
  // unless the pin is isolated (ESP-IDF sleep-modes docs, WROVER note).
  rtc_gpio_isolate(GPIO_NUM_12);
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(50);  // enough for the UART to settle; 200 ms was pure awake-time tax
  app::g_health.resetReason = (uint32_t)esp_reset_reason();
  Serial.println("\n[airdeck] wake (battery profile)");

  // A cold boot (power-on / reset) clears the panel; a timer wake keeps the
  // existing image so an unchanged reading can skip the refresh entirely.
  const bool coldBoot = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
  // An unclean previous wake (crash mid-draw) also forces a clear: the panel
  // may hold a half-written image the signature knows nothing about.
  const bool preserved = !coldBoot && rtcCleanShutdown;
  rtcCleanShutdown = false;  // mark this wake "in progress"

  // Radio FIRST, and without waiting: WiFi.begin() associates in the
  // background, so the display/ADC work below overlaps the 1-4 s of
  // association + (skipped, when the fast hint holds) scan/DHCP instead of
  // running before it. The hint comes from the previous wake via RTC memory.
  adapters::WifiLink wifi(cfg::kWifiSsid, cfg::kWifiPass);
  adapters::AirGradientHttp source(cfg::kMonitorUrl);
  if (!coldBoot && rtcWifiHint.valid) wifi.setFastHint(rtcWifiHint);
  wifi.begin();
  wifi.startConnect();

  // The display is NOT brought up yet: roughly half the wakes end in "skip
  // refresh (unchanged)" and never need epd_init, the 259 KB framebuffer or
  // ui::build() at all. ensureDisplay() runs it on first actual need.
  bool displayUp = false;
  auto ensureDisplay = [&]() -> bool {
    if (!displayUp)
      displayUp = bringUpDisplay(/*clearPanel=*/!preserved,
                                 /*nvsHygiene=*/false);
    return displayUp;
  };

  float vbat = app::batteryVoltage();
  Serial.printf("[batt] %.2f V (%d%%)\n", vbat, app::batteryPercent());

  // Brownout gate: too low to safely drive a waveform -> show a static charge
  // screen once and sleep long. vbat<=0.5 means "no divider / on USB". The
  // 50 mV margin means the one "CHARGE ME" waveform itself still runs above
  // kBatteryLowV — never below the very line that deems refreshes unsafe.
  if (vbat > 0.5f && vbat < app::kBatteryLowV + 0.05f) {
    if (ensureDisplay()) {
      ui::showMessage("CHARGE ME", "Battery low", "");
      lv_refr_now(g_disp);
      rtcLastSig = 0;  // force a real redraw once the pack recovers
      rtcHasImage = true;
    }
    sleepFor(cfg::kBatteryLowSleepSeconds, displayUp);
  }

  app::Snapshot snap;
  bool pollOk = false;
  if (wifi.ensureConnected()) {  // waits out the remainder of startConnect()
    snap.rssi = wifi.rssi();
    wifi.captureFastHint(rtcWifiHint);  // next wake skips scan + DHCP

    // SNTP is async and radio-chatty (DNS + UDP each wake), while system time
    // itself survives deep sleep on the RTC slow clock — drift is seconds/day,
    // invisible on an HH:MM display. Sync on cold boot, when never synced, or
    // daily; otherwise just restore the TZ that deep sleep wiped.
    const bool needNtp = coldBoot || !app::timeIsSynced() ||
                         (time(nullptr) - rtcLastNtpSync) > 24L * 3600L;
    if (needNtp)
      configTime(cfg::kGmtOffsetSec, cfg::kDstOffsetSec, "pool.ntp.org",
                 "time.nist.gov");
    else
      restoreTimezone();

    domain::Measurement m;
    if (source.poll(m)) {
      snap.m = m;
      snap.state = app::NetState::Online;
      snap.lastOkMs = millis();
      snap.pollCount = 1;
      g_guard.setAmbientTempC(m.tempC());
      pollOk = true;
      Serial.printf("[net] ok  CO2=%.0f PM2.5=%.1f T=%.1f\n", m.rco2, m.pm02,
                    m.tempC());
    } else {
      snap.state = app::NetState::Offline;
      Serial.printf("[net] poll failed: %s\n", source.lastError());
    }
    if (needNtp && app::timeIsSynced()) rtcLastNtpSync = time(nullptr);
  } else {
    rtcWifiHint.valid = false;  // don't retry a hint that just failed
    snap.state = app::NetState::Offline;
  }

  // Decide whether this wake earns a full-screen refresh.
  const uint32_t sig = pollOk ? measurementSignature(snap.m) : 0;
  const bool changed = pollOk && sig != rtcLastSig;
  const bool backstop = cfg::kBatteryFullRefreshEvery != 0 &&
                        rtcWakeCount % cfg::kBatteryFullRefreshEvery == 0;
  // Always draw the very first frame; otherwise only when data changed or the
  // periodic backstop is due. A failed poll never redraws — it would just
  // reprint the same numbers and burn a waveform. A wake whose previous
  // shutdown was unclean must redraw too: the panel content is untrusted.
  const bool doRefresh =
      !rtcHasImage || !preserved || (pollOk && (changed || backstop));

  if (doRefresh) {
    if (!ensureDisplay()) {
      // Nothing to draw with. Don't touch the (uninitialised) panel rails —
      // just sleep out the normal interval and retry from a fresh boot.
      Serial.println("[fatal] display bring-up failed — sleeping");
      Serial.flush();
      esp_sleep_enable_timer_wakeup((uint64_t)cfg::kBatteryPollSeconds *
                                    1000000ULL);
      esp_deep_sleep_start();
    }
    ui::update(snap);     // refresh page models (built by ensureDisplay)
    ui::showMainNow();    // leave splash, show the dashboard, full-invalidate
    lv_refr_now(g_disp);  // render + flush synchronously before sleeping
    rtcHasImage = true;
    if (pollOk) rtcLastSig = sig;
    Serial.printf("[epd] refresh (%s)\n",
                  !changed ? "backstop/first" : "changed");
  } else {
    Serial.println("[epd] skip refresh (unchanged)");
  }

  rtcWakeCount++;
  sleepFor(cfg::kBatteryPollSeconds, displayUp);
}

void loop() {}  // never reached — deep sleep restarts from setup()

#endif
