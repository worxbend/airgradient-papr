// src/adapters/net_services.cpp
#include "adapters/net_services.hpp"

#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "app/health.hpp"

namespace adapters {
namespace {

WebServer server(80);
bool started = false;
volatile bool repairRequested = false;

void handleHealth() {
  const app::HealthStats& h = app::g_health;
  uint32_t lastOkAgo = h.lastOkMs ? (millis() - h.lastOkMs) / 1000 : 0;

  // largest_free_block is the number that actually predicts the panel-driver
  // deadlock: its per-frame worker stacks need 8 KB *contiguous* internal DRAM,
  // which free_heap alone does not guarantee on a fragmented heap.
  uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint32_t netAgo = h.netTickMs ? (millis() - h.netTickMs) / 1000 : 0;

  char buf[768];
  int n = snprintf(buf, sizeof(buf),
                   "{"
                   "\"uptime_s\":%lu,"
                   "\"reset_reason\":%lu,"
                   "\"free_heap\":%u,"
                   "\"min_free_heap\":%u,"
                   "\"largest_free_block\":%lu,"
                   "\"free_psram\":%u,"
                   "\"rssi\":%d,"
                   "\"poll_count\":%lu,"
                   "\"consecutive_fail\":%lu,"
                   "\"last_poll_ago_s\":%lu,"
                   "\"net_tick_ago_s\":%lu,"
                   "\"full_refreshes\":%lu,"
                   "\"refresh_deferred_rate\":%lu,"
                   "\"refresh_deferred_heap\":%lu,"
                   "\"refresh_deferred_temp\":%lu,"
                   "\"last_refresh_ms\":%lu,"
                   "\"max_refresh_ms\":%lu,"
                   "\"sensor_fw\":\"%s\","
                   "\"ip\":\"%s\""
                   "}",
                   (unsigned long)(millis() / 1000),
                   (unsigned long)h.resetReason, (unsigned)ESP.getFreeHeap(),
                   (unsigned)ESP.getMinFreeHeap(), (unsigned long)largest,
                   (unsigned)ESP.getFreePsram(), h.rssi,
                   (unsigned long)h.pollCount, (unsigned long)h.consecutiveFail,
                   (unsigned long)lastOkAgo, (unsigned long)netAgo,
                   (unsigned long)h.fullRefreshes,
                   (unsigned long)h.refreshDeferredRate,
                   (unsigned long)h.refreshDeferredHeap,
                   (unsigned long)h.refreshDeferredTemp,
                   (unsigned long)h.lastRefreshMs,
                   (unsigned long)h.maxRefreshMs, h.sensorFirmware,
                   WiFi.localIP().toString().c_str());
  (void)n;
  server.send(200, "application/json", buf);
}

}  // namespace

void netServicesBegin(const char* hostname) {
  if (started) return;
  started = true;

  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() { Serial.println("[ota] start"); });
  ArduinoOTA.onEnd([]() { Serial.println("[ota] end"); });
  ArduinoOTA.onError(
      [](ota_error_t e) { Serial.printf("[ota] error %u\n", e); });
  ArduinoOTA.begin();

  server.on("/health", handleHealth);
  // On-demand de-ghost. Only flags the request — EpdGuard::repairRoutine()
  // drives the panel directly and must run on the UI task (see its docs).
  server.on("/repair", []() {
    repairRequested = true;
    server.send(202, "text/plain", "de-ghost queued\n");
  });
  server.on("/", []() {
    server.send(200, "text/plain",
                "airdeck — GET /health for status, /repair to de-ghost\n");
  });
  server.begin();

  Serial.printf("[svc] OTA + /health up on %s (%s)\n", hostname,
                WiFi.localIP().toString().c_str());
}

bool netServicesTakeRepairRequest() {
  bool v = repairRequested;
  repairRequested = false;
  return v;
}

void netServicesHandle() {
  if (!started) return;
  ArduinoOTA.handle();
  server.handleClient();
}

}  // namespace adapters
