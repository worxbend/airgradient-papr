// src/adapters/wifi_link.cpp
#include "adapters/wifi_link.hpp"

#include <WiFi.h>

namespace adapters {

static constexpr uint32_t kBackoffCapMs = 60000;
static constexpr uint32_t kConnectWindowMs = 8000;

void WifiLink::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // snappy polls while awake (§2b)
  WiFi.setAutoReconnect(true);
  started_ = true;
  nextAttemptAt_ = 0;  // attempt immediately
}

bool WifiLink::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

int WifiLink::rssi() const {
  return connected() ? WiFi.RSSI() : 0;
}

bool WifiLink::ensureConnected() {
  if (!started_) begin();
  if (connected()) {
    backoffMs_ = 1000;  // reset backoff once healthy
    return true;
  }

  uint32_t now = millis();
  if (now < nextAttemptAt_) return false;  // still backing off

  WiFi.disconnect(true, false);
  delay(50);
  WiFi.begin(ssid_, pass_);

  uint32_t start = millis();
  while (millis() - start < kConnectWindowMs) {
    if (WiFi.status() == WL_CONNECTED) {
      backoffMs_ = 1000;
      return true;
    }
    delay(100);  // yields to other tasks
  }

  // Failed this window — schedule next attempt with exponential backoff.
  failures_++;
  nextAttemptAt_ = millis() + backoffMs_;
  backoffMs_ = (backoffMs_ >= kBackoffCapMs) ? kBackoffCapMs : backoffMs_ * 2;
  return false;
}

}  // namespace adapters
