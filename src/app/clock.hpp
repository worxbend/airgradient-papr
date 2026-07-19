// src/app/clock.hpp
// Wall-clock helpers shared across modules.
//
// NTP synchronisation is asynchronous: after configTime() the SNTP client
// fetches the time in the background, so for the first few seconds after boot
// time(nullptr) still returns the RTC's power-on value (near the 1970 epoch).
// Anything time-dependent — the on-screen clock, the currency history date
// range, the weather "now" slot — must therefore gate on a *synced* clock.
//
// This centralises that test so the sentinel epoch lives in exactly one place
// instead of being repeated as a bare magic number (1700000000) across
// main.cpp, dashboard.cpp, weather_http.cpp and currency_http.cpp.
#pragma once
#include <ctime>

namespace app {

// Unix time for 2023-11-14 22:13:20 UTC. Any timestamp greater than this proves
// SNTP has delivered a real wall-clock time; the RTC powers up near 0.
inline constexpr time_t kClockSyncedAfter = 1700000000;

// True once NTP has set the wall clock. Cheap (a single time() call).
inline bool timeIsSynced() {
  return time(nullptr) > kClockSyncedAfter;
}

}  // namespace app
