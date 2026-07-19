// include/config.example.hpp
// Copy to include/config.hpp (gitignored) and fill in real values.
//
//   cp include/config.example.hpp include/config.hpp
//
// Per PLAN.md §2b: Wi-Fi credentials and the AirGradient address are
// COMPILE-TIME constants. No captive portal, no runtime provisioning.
// Give the AirGradient a DHCP reservation so the fixed IP never drifts.
#pragma once
#include <cstdint>

namespace cfg {

// ---- Wi-Fi (2.4 GHz only) --------------------------------------------------
constexpr char kWifiSsid[] = "YOUR_WIFI_SSID";
constexpr char kWifiPass[] = "YOUR_WIFI_PASSWORD";

// ---- AirGradient ONE local-server API --------------------------------------
// Fixed-IP endpoint (recommended; give the monitor a DHCP reservation).
// Full URL to the "current measures" endpoint.
constexpr char kMonitorUrl[] = "http://192.168.1.21/measures/current";

// Optional: the monitor serial, for the mDNS fallback name
// airgradient_{SERIAL}.local. Leave empty to use kMonitorUrl only.
constexpr char kMonitorSerial[] = "";

// ---- Outdoor weather + UV (Open-Meteo, no API key) -------------------------
// Location: leave both at 0 to auto-detect via IP geolocation (ip-api.com).
// Set explicit coordinates to override.
constexpr double kLatitude = 0.0;
constexpr double kLongitude = 0.0;
constexpr uint32_t kWeatherPollSeconds = 600;    // 10 min
constexpr uint32_t kCurrencyPollSeconds = 1800;  // 30 min
constexpr uint32_t kPageRefreshSeconds = 30;     // whole-page redraw interval

// ---- Behaviour -------------------------------------------------------------
constexpr uint32_t kPollSeconds = 30;    // 15..300
constexpr uint8_t kNightStartHour = 23;  // inclusive
constexpr uint8_t kNightEndHour = 7;     // exclusive
constexpr bool kFahrenheit = false;

// ---- Battery profile (-DPROFILE_BATTERY only) ------------------------------
// Wall power ignores all of these. On battery the board is a single-shot
// wake->poll->render->deep-sleep loop, so the wake cadence dominates runtime:
// waking every kPollSeconds (30 s) would flatten a LiPo in ~a day, so battery
// gets its own, much longer interval. See handbook §2 (Power model).
constexpr uint32_t kBatteryPollSeconds = 900;  // wake cadence on battery (15 min)
// Even when the air reading is byte-identical to what's already on the panel,
// force a full GC16 redraw every Nth wake — refreshes the clock and clears any
// e-paper ghosting. 0 disables the "skip unchanged" optimisation entirely.
constexpr uint32_t kBatteryFullRefreshEvery = 4;  // ~hourly at 15-min wakes
// How long to sleep when the pack is too low to safely drive a waveform.
constexpr uint32_t kBatteryLowSleepSeconds = 1800;  // 30 min

// Timezone offset from UTC in seconds, used only for the on-screen clock
// (e.g. UTC+3 = 10800). Auto-detection is unreliable, so set it here.
constexpr long kGmtOffsetSec = 0;
constexpr int kDstOffsetSec = 0;

}  // namespace cfg
