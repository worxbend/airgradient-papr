// src/adapters/wifi_link.hpp
// Wi-Fi lifecycle with exponential-backoff reconnect.
// No captive portal, no mDNS — fixed compile-time credentials.
#pragma once
#include <Arduino.h>

namespace adapters {

// Connection parameters captured after a successful associate + DHCP, so a
// deep-sleep wake can skip the all-channel scan (channel + BSSID) and the DHCP
// exchange (static lease) — the difference between ~3-5 s and ~1 s of radio-on
// time per wake. The caller owns persistence (RTC slow memory on battery);
// `valid` false means "connect the slow, always-works way".
struct WifiFastHint {
  bool valid = false;
  uint8_t bssid[6] = {0};
  int32_t channel = 0;
  uint32_t ip = 0, gateway = 0, subnet = 0, dns = 0;  // IPv4, network order
};

class WifiLink {
 public:
  WifiLink(const char* ssid, const char* pass) : ssid_(ssid), pass_(pass) {}

  void begin();

  // Try the hinted channel/BSSID/static-lease path on the next attempt. If
  // that attempt misses its (shorter) window — AP rebooted, moved channel,
  // changed the lease — the link falls back to a full scan + DHCP by itself.
  void setFastHint(const WifiFastHint& h) { hint_ = h; }

  // Snapshot the current connection into `out` (true only when connected).
  bool captureFastHint(WifiFastHint& out) const;

  // Fire off an association attempt WITHOUT waiting for it — WiFi.begin() is
  // asynchronous, so the radio associates while the caller does other work
  // (display bring-up on a battery wake). A later ensureConnected() notices
  // the in-flight attempt and only waits out the remaining window instead of
  // restarting from scratch. Safe to call when already connected (no-op).
  void startConnect();

  // Call from the net task. Returns true when connected. Blocks (with yields)
  // for at most one connect attempt window; respects backoff between attempts.
  bool ensureConnected();

  bool connected() const;
  int rssi() const;
  uint32_t failures() const { return failures_; }

 private:
  const char* ssid_;
  const char* pass_;
  WifiFastHint hint_;
  bool attemptInFlight_ = false;
  uint32_t attemptStartMs_ = 0;
  uint32_t backoffMs_ = 1000;
  uint32_t nextAttemptAt_ = 0;
  uint32_t failures_ = 0;
  bool started_ = false;
};

}  // namespace adapters
