// src/main.cpp — composition root (PLAN.md §D5, §8.1).
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
#include <esp_sleep.h>

#include "app/power.hpp"
#endif

static adapters::EpdGuard g_guard;
static lv_display_t* g_disp = nullptr;

// clearPanel=false preserves the on-panel image across a battery timer wake
// (see EpdGuard::init); the USB profile always clears.
static bool bringUpDisplay(bool clearPanel = true) {
  bool ok = true;
  if (!psramFound()) {
    Serial.println("[fatal] PSRAM not found — check board/build flags");
    ok = false;
  } else {
    Serial.printf("[boot] PSRAM: %u bytes free\n",
                  (unsigned)ESP.getFreePsram());
  }
  if (!g_guard.init(clearPanel)) {
    Serial.println("[fatal] EPD framebuffer alloc failed");
    ok = false;
  }
  g_disp = adapters::epdLvglInit(&g_guard);
  if (!g_disp) {
    Serial.println("[fatal] LVGL display init failed");
    ok = false;
  }
  ui::build();
  return ok;
}

// ===========================================================================
#if !defined(PROFILE_BATTERY)  // ---- USB always-on profile ----------------
// ===========================================================================
static QueueHandle_t g_mailbox = nullptr;
static app::Buttons g_buttons;

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
  for (;;) {
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
      fetchWeather(wx, snap, geoDone, lat, lon);
      xQueueOverwrite(g_mailbox, &snap);
    }

    uint32_t nowC = millis();
    bool timeReady = app::timeIsSynced();
    if ((lastCurrency == 0 && timeReady) ||
        (lastCurrency != 0 &&
         nowC - lastCurrency >= cfg::kCurrencyPollSeconds * 1000UL)) {
      lastCurrency = nowC;
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
  Serial.println("\n[airdeck] boot (USB profile)");
  bringUpDisplay();
  lv_timer_handler();  // render the (empty-state) main page now, while heap is
                       // high

  g_buttons.begin();
  g_mailbox = xQueueCreate(1, sizeof(app::Snapshot));
  // 16 KB stack: mbedTLS (HTTPS weather fetch) needs the headroom.
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 4, nullptr, 0);
  Serial.printf("[boot] heap free: %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  static app::Snapshot snap;
  static uint32_t lastRefresh = 0;
  app::Snapshot incoming;
  if (xQueueReceive(g_mailbox, &incoming, 0) == pdTRUE) {
    snap = incoming;
    if (snap.m.valid) g_guard.setAmbientTempC(snap.m.tempC());
    ui::update(snap);
  }

  // Periodic whole-page refresh: keep time-based fields current and give the
  // panel a clean redraw at a fixed interval, on whatever page is showing.
  if (millis() - lastRefresh >= cfg::kPageRefreshSeconds * 1000UL) {
    lastRefresh = millis();
    ui::update(snap);
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

  app::g_health.fullRefreshes = g_guard.fullRefreshes();
  app::g_health.partialRefreshes = g_guard.partialRefreshes();
  lv_timer_handler();
  delay(5);
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
// See handbook §2 (Power model) for the numbers.

// RTC slow-memory state — survives deep sleep, lost on power-cut / reset.
RTC_DATA_ATTR static uint32_t rtcLastSig = 0;  // signature of on-panel reading
RTC_DATA_ATTR static uint32_t rtcWakeCount = 0;  // wakes since cold boot
RTC_DATA_ATTR static bool rtcHasImage = false;  // has the panel ever been drawn

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

static void sleepFor(uint32_t seconds) {
  g_guard
      .markCleanShutdown();    // clean flag -> next wake may preserve the image
  g_guard.powerOffForSleep();  // rails + peripherals down (§6.1.3)
  WiFi.disconnect(true, false);
  Serial.printf("[sleep] %lus\n", (unsigned long)seconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[airdeck] wake (battery profile)");

  // A cold boot (power-on / reset) clears the panel; a timer wake keeps the
  // existing image so an unchanged reading can skip the refresh entirely.
  const bool coldBoot = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER;
  bringUpDisplay(/*clearPanel=*/coldBoot);

  float vbat = app::batteryVoltage();
  Serial.printf("[batt] %.2f V (%d%%)\n", vbat, app::batteryPercent());

  // Brownout gate (§6.1.4): too low to safely drive a waveform -> show a static
  // charge screen once and sleep long. vbat<=0.5 means "no divider / on USB".
  if (vbat > 0.5f && vbat < app::kBatteryLowV) {
    ui::showMessage("CHARGE ME", "Battery low", "");
    lv_refr_now(g_disp);
    rtcLastSig = 0;  // force a real redraw once the pack recovers
    rtcHasImage = true;
    sleepFor(cfg::kBatteryLowSleepSeconds);
  }

  adapters::WifiLink wifi(cfg::kWifiSsid, cfg::kWifiPass);
  adapters::AirGradientHttp source(cfg::kMonitorUrl);
  wifi.begin();
  configTime(cfg::kGmtOffsetSec, cfg::kDstOffsetSec, "pool.ntp.org",
             "time.nist.gov");

  app::Snapshot snap;
  bool pollOk = false;
  if (wifi.ensureConnected()) {
    snap.rssi = wifi.rssi();
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
  } else {
    snap.state = app::NetState::Offline;
  }

  ui::update(snap);  // refresh page models (no panel I/O by itself)

  // Decide whether this wake earns a full-screen refresh.
  const uint32_t sig = pollOk ? measurementSignature(snap.m) : 0;
  const bool changed = pollOk && sig != rtcLastSig;
  const bool backstop = cfg::kBatteryFullRefreshEvery != 0 &&
                        rtcWakeCount % cfg::kBatteryFullRefreshEvery == 0;
  // Always draw the very first frame; otherwise only when data changed or the
  // periodic backstop is due. A failed poll never redraws — it would just
  // reprint the same numbers and burn a waveform.
  const bool doRefresh = !rtcHasImage || (pollOk && (changed || backstop));

  if (doRefresh) {
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
  sleepFor(cfg::kBatteryPollSeconds);
}

void loop() {}  // never reached — deep sleep restarts from setup()

#endif
