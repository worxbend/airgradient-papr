// src/app/power.hpp
// Battery ADC + sleep policy for the optional -DPROFILE_BATTERY build
// (PLAN.md §T5, §8.1). On USB the always-on profile ignores this.
#pragma once
#include <cstdint>

namespace app {

// Below this the guard refuses full refreshes (brownout mid-waveform =
// DC-imbalanced drive = panel damage — PLAN.md §6.1.4).
inline constexpr float kBatteryLowV = 3.4f;

float batteryVoltage();  // volts (0 if no divider / not readable)
int   batteryPercent();  // 0..100, clamped

}  // namespace app
