// src/app/snapshot.hpp
// The single value handed from net_task -> ui_task through a length-1 mailbox
// queue (PLAN.md §D5). Plain-old-data so it copies by value cleanly.
#pragma once
#include "domain/currency.hpp"
#include "domain/measurement.hpp"
#include "domain/weather.hpp"

namespace app {

enum class NetState : uint8_t { Booting, Connecting, Online, Stale, Offline };

struct Snapshot {
  domain::Measurement m;      // latest good AirGradient reading
  domain::Weather weather;    // latest outdoor weather + UV (Open-Meteo)
  domain::Currency currency;  // exchange rates + USD/UAH history
  NetState state = NetState::Booting;
  int rssi = 0;           // ESP32's own RSSI to the AP, dBm
  uint32_t lastOkMs = 0;  // millis() of last successful poll (0 = never)
  uint32_t consecutiveFail = 0;
  uint32_t pollCount = 0;
};

}  // namespace app
