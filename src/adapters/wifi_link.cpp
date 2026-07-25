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

  // Rollover-safe "is the backoff over?". A plain `now < nextAttemptAt_` breaks
  // at the 49.7-day millis() wrap: with a deadline near UINT32_MAX and `now`
  // freshly wrapped to a small value the comparison stays true, so the link
  // would refuse to retry for another ~49 days — a board that had been up for
  // weeks would simply stop reconnecting. Comparing the *difference* as a
  // signed value is correct across the wrap.
  uint32_t now = millis();
  if ((int32_t)(now - nextAttemptAt_) < 0) return false;  // still backing off

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
