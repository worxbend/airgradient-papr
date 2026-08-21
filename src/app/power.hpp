// src/app/power.hpp
// Battery ADC + sleep policy for the optional -DPROFILE_BATTERY build.
// On USB the always-on profile ignores this.
#pragma once

namespace app {

// Below this the guard refuses full refreshes (brownout mid-waveform =
// DC-imbalanced drive = panel damage).
inline constexpr float kBatteryLowV = 3.4f;

// Endpoints of the percentage scale. They live next to kBatteryLowV because the
// three numbers only make sense together: the "empty" end must stay below the
// line at which refreshes are refused, or the display would claim 0% while the
// guard still considered the pack safe to drive (and vice versa).
inline constexpr float kBatteryEmptyV = 3.30f;
inline constexpr float kBatteryFullV = 4.20f;

float batteryVoltage();  // volts (0 if no divider / not readable)
int batteryPercent();    // 0..100, clamped

// Same mapping for a voltage the caller already measured. Callers that log both
// the voltage and the percentage should use this so the two numbers come from
// one ADC average instead of two separate ones taken moments apart.
int batteryPercent(float volts);  // 0..100, clamped

}  // namespace app
