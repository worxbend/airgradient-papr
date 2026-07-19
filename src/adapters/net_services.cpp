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

void handleHealth() {
  const app::HealthStats& h = app::g_health;
  uint32_t lastOkAgo = h.lastOkMs ? (millis() - h.lastOkMs) / 1000 : 0;

  char buf[512];
  int n = snprintf(buf, sizeof(buf),
                   "{"
                   "\"uptime_s\":%lu,"
                   "\"free_heap\":%u,"
                   "\"min_free_heap\":%u,"
                   "\"free_psram\":%u,"
                   "\"rssi\":%d,"
                   "\"poll_count\":%lu,"
                   "\"consecutive_fail\":%lu,"
                   "\"last_poll_ago_s\":%lu,"
                   "\"full_refreshes\":%lu,"
                   "\"partial_refreshes\":%lu,"
                   "\"sensor_fw\":\"%s\","
                   "\"ip\":\"%s\""
                   "}",
                   (unsigned long)(millis() / 1000),
                   (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                   (unsigned)ESP.getFreePsram(), h.rssi,
                   (unsigned long)h.pollCount, (unsigned long)h.consecutiveFail,
                   (unsigned long)lastOkAgo, (unsigned long)h.fullRefreshes,
                   (unsigned long)h.partialRefreshes, h.sensorFirmware,
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
  server.on(
      "/", []() { server.send(200, "text/plain", "airdeck — see /health\n"); });
  server.begin();

  Serial.printf("[svc] OTA + /health up on %s (%s)\n", hostname,
                WiFi.localIP().toString().c_str());
}

void netServicesHandle() {
  if (!started) return;
  ArduinoOTA.handle();
  server.handleClient();
}

}  // namespace adapters
