// src/adapters/wifi_link.cpp
#include "adapters/wifi_link.hpp"

#include <WiFi.h>

namespace adapters {

static constexpr uint32_t kBackoffCapMs = 60000;
static constexpr uint32_t kConnectWindowMs = 8000;
// The hinted path needs no scan or DHCP, so a miss is knowable much sooner.
static constexpr uint32_t kFastConnectWindowMs = 3000;

void WifiLink::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
#if !defined(PROFILE_BATTERY)
  // Disable modem power-save only on wall power: snappy polls matter there.
  // On battery the awake window is a single HTTP GET — the default modem
  // sleep costs nothing noticeable and saves radio power while associated.
  WiFi.setSleep(false);
#endif
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
  if (!attemptInFlight_ && (int32_t)(now - nextAttemptAt_) < 0)
    return false;  // still backing off

  // Fast path: hinted channel + BSSID skips the all-channel scan, and the
  // remembered lease skips DHCP — so a miss is knowable in a shorter window.
  // One try only: if the AP changed anything the hint is dropped and the plain
  // scan + DHCP path runs immediately below.
  const bool usedHint = hint_.valid;
  startConnect();  // no-op if a startConnect() call already kicked this off
  const uint32_t window = usedHint ? kFastConnectWindowMs : kConnectWindowMs;
  while (millis() - attemptStartMs_ < window) {
    if (WiFi.status() == WL_CONNECTED) {
      backoffMs_ = 1000;
      attemptInFlight_ = false;
      return true;
    }
    delay(100);  // yields to other tasks
  }
  attemptInFlight_ = false;

  if (usedHint) {
    // Stale hint (AP rebooted / new channel / new lease). Revert to DHCP and
    // retry the normal way right now — no backoff for a hint miss.
    hint_.valid = false;
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);  // DHCP again
    startConnect();
    while (millis() - attemptStartMs_ < kConnectWindowMs) {
      if (WiFi.status() == WL_CONNECTED) {
        backoffMs_ = 1000;
        attemptInFlight_ = false;
        return true;
      }
      delay(100);
    }
    attemptInFlight_ = false;
  }

  // Failed this window — schedule next attempt with exponential backoff.
  failures_++;
  nextAttemptAt_ = millis() + backoffMs_;
  backoffMs_ = (backoffMs_ >= kBackoffCapMs) ? kBackoffCapMs : backoffMs_ * 2;
  return false;
}

void WifiLink::startConnect() {
  if (!started_) begin();
  if (connected() || attemptInFlight_) return;
  WiFi.disconnect(true, false);
  delay(50);
  if (hint_.valid) {
    WiFi.config(IPAddress(hint_.ip), IPAddress(hint_.gateway),
                IPAddress(hint_.subnet), IPAddress(hint_.dns));
    WiFi.begin(ssid_, pass_, hint_.channel, hint_.bssid);
  } else {
    WiFi.begin(ssid_, pass_);
  }
  attemptInFlight_ = true;
  attemptStartMs_ = millis();
}

bool WifiLink::captureFastHint(WifiFastHint& out) const {
  if (!connected()) return false;
  const uint8_t* b = WiFi.BSSID();
  if (!b) return false;
  memcpy(out.bssid, b, 6);
  out.channel = WiFi.channel();
  out.ip = (uint32_t)WiFi.localIP();
  out.gateway = (uint32_t)WiFi.gatewayIP();
  out.subnet = (uint32_t)WiFi.subnetMask();
  out.dns = (uint32_t)WiFi.dnsIP();
  out.valid = out.ip != 0 && out.channel > 0;
  return out.valid;
}

}  // namespace adapters
