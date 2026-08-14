// include/config.example.hpp
// Copy to include/config.hpp (gitignored) and fill in real values.
//
//   cp include/config.example.hpp include/config.hpp
//
// Wi-Fi credentials and the AirGradient address are COMPILE-TIME constants.
// No captive portal, no runtime provisioning.
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

// ---- Display refresh guards (panel protection) -----------------------------
// One full GC16 update costs ~47 panel frames, blocks the UI task for ~1 s and
// peaks the panel at up to 950 mW (ED047TC1 datasheet §6-2). The guards below
// cap how often the panel may be driven, no matter how chatty the data is.
//
// kMinRefreshIntervalSeconds is the HARD floor between two panel updates: the
// framebuffer keeps accumulating changes and the newest image is pushed at the
// next allowed slot. 60 s matches the finest thing on screen that actually
// moves (the HH:MM clock) — raise it to 300 for a much quieter panel at the
// cost of a clock that can read up to 5 minutes stale.
constexpr uint32_t kMinRefreshIntervalSeconds = 60;
// Floor for user-initiated pushes (a button page switch). Short enough that
// paging feels instant, long enough that a mashed button can't burn one
// waveform per press.
constexpr uint32_t kForcedRefreshMinSeconds = 3;
// How often the page models are recomputed (clock, "last seen", bands). Pure
// CPU — this does NOT touch the panel; it only decides *what* the next allowed
// refresh will show.
constexpr uint32_t kUiTickSeconds = 15;
// De-ghost backstop: repaint the whole page this often even if nothing changed,
// to clear e-paper particle memory. 0 disables.
constexpr uint32_t kDeGhostRefreshSeconds = 3600;  // hourly

// ---- Fault recovery --------------------------------------------------------
// Hardware task watchdog on the UI task: if the panel driver deadlocks (its
// per-frame xTaskCreate can fail under a fragmented heap and then block forever
// on a semaphore), the board reboots itself instead of waiting for a human.
// Must comfortably exceed the worst-case refresh (~1-2 s).
constexpr uint32_t kWatchdogSeconds = 60;
// Reboot if the net task stops making progress for this long (worst legitimate
// case is a retried wttr.in fetch plus a connect window).
constexpr uint32_t kNetStallRebootSeconds = 600;
// Reboot if the panel's heap gate has been continuously starving a pending
// refresh for this long. Internal-DRAM fragmentation only ever heals with a
// reboot — without this backstop a permanently closed gate is a silent,
// forever-frozen screen with Wi-Fi still up. Transient TLS spikes clear in
// seconds, so 10 minutes of continuous starvation means it is not coming back.
constexpr uint32_t kRefreshStarveRebootSeconds = 600;

// ---- Behaviour -------------------------------------------------------------
constexpr uint32_t kPollSeconds = 30;    // 15..300
constexpr uint8_t kNightStartHour = 23;  // inclusive
constexpr uint8_t kNightEndHour = 7;     // exclusive
constexpr bool kFahrenheit = false;

// ---- Battery profile (-DPROFILE_BATTERY only) ------------------------------
// Wall power ignores all of these. On battery the board is a single-shot
// wake->poll->render->deep-sleep loop, so the wake cadence dominates runtime:
// waking every kPollSeconds (30 s) would flatten a LiPo in ~a day, so battery
// gets its own, much longer interval. See handbook ch. 02 (Power model).
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
