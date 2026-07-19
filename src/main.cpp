// src/main.cpp — composition root (PLAN.md §D5, §8.1).
//
// Default (USB always-on): net_task (core 0) polls Wi-Fi/HTTP into a length-1
// mailbox; the Arduino loop (core 1) renders LVGL + drives the panel. All LVGL
// calls stay on the UI task.
//
// -DPROFILE_BATTERY: single-shot — wake, connect, poll, render, power the panel
// fully off, deep-sleep for the poll interval.
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

static bool bringUpDisplay() {
  bool ok = true;
  if (!psramFound()) {
    Serial.println("[fatal] PSRAM not found — check board/build flags");
    ok = false;
  } else {
    Serial.printf("[boot] PSRAM: %u bytes free\n",
                  (unsigned)ESP.getFreePsram());
  }
  if (!g_guard.init()) {
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
    bool timeReady = time(nullptr) > 1700000000;
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
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[airdeck] wake (battery profile)");
  bringUpDisplay();

  float vbat = app::batteryVoltage();
  Serial.printf("[batt] %.2f V (%d%%)\n", vbat, app::batteryPercent());

  // Brownout gate (§6.1.4): too low to safely drive a waveform -> show a static
  // charge screen once and sleep long.
  if (vbat > 0.5f && vbat < app::kBatteryLowV) {
    ui::showMessage("CHARGE ME", "Battery low", "");
    lv_refr_now(g_disp);
    g_guard.markCleanShutdown();
    g_guard.powerOffForSleep();
    esp_sleep_enable_timer_wakeup((uint64_t)600 * 1000000ULL);  // 10 min
    esp_deep_sleep_start();
  }

  adapters::WifiLink wifi(cfg::kWifiSsid, cfg::kWifiPass);
  adapters::AirGradientHttp source(cfg::kMonitorUrl);
  wifi.begin();
  configTime(cfg::kGmtOffsetSec, cfg::kDstOffsetSec, "pool.ntp.org",
             "time.nist.gov");

  app::Snapshot snap;
  snap.state = app::NetState::Connecting;
  ui::showMessage("AIRDECK", "Connecting", "");
  lv_refr_now(g_disp);

  if (wifi.ensureConnected()) {
    snap.rssi = wifi.rssi();
    domain::Measurement m;
    if (source.poll(m)) {
      snap.m = m;
      snap.state = app::NetState::Online;
      snap.lastOkMs = millis();
      snap.pollCount = 1;
      g_guard.setAmbientTempC(m.tempC());
      Serial.printf("[net] ok  CO2=%.0f PM2.5=%.1f T=%.1f\n", m.rco2, m.pm02,
                    m.tempC());
    } else {
      snap.state = app::NetState::Offline;
      Serial.printf("[net] poll failed: %s\n", source.lastError());
    }
  } else {
    snap.state = app::NetState::Offline;
  }

  ui::update(snap);
  lv_refr_now(g_disp);  // render + flush synchronously before sleeping

  // Panel + peripherals fully off (§6.1.3) — the image persists with no power.
  g_guard.markCleanShutdown();
  g_guard.powerOffForSleep();
  WiFi.disconnect(true, false);

  uint64_t us = (uint64_t)cfg::kPollSeconds * 1000000ULL;
  Serial.printf("[sleep] %lus\n", (unsigned long)cfg::kPollSeconds);
  Serial.flush();
  esp_sleep_enable_timer_wakeup(us);
  esp_deep_sleep_start();
}

void loop() {}  // never reached — deep sleep restarts from setup()

#endif
