// src/adapters/wifi_link.hpp
// Wi-Fi lifecycle with exponential-backoff reconnect (PLAN.md §2b).
// No captive portal, no mDNS — fixed compile-time credentials.
#pragma once
#include <Arduino.h>

namespace adapters {

class WifiLink {
 public:
  WifiLink(const char* ssid, const char* pass) : ssid_(ssid), pass_(pass) {}

  void begin();

  // Call from the net task. Returns true when connected. Blocks (with yields)
  // for at most one connect attempt window; respects backoff between attempts.
  bool ensureConnected();

  bool connected() const;
  int  rssi() const;
  uint32_t failures() const { return failures_; }

 private:
  const char* ssid_;
  const char* pass_;
  uint32_t backoffMs_ = 1000;
  uint32_t nextAttemptAt_ = 0;
  uint32_t failures_ = 0;
  bool started_ = false;
};

}  // namespace adapters
