// src/app/power.cpp
#include "app/power.hpp"

#include <Arduino.h>

namespace app {

// LilyGo T5-4.7 V1 (classic ESP32): battery sense on GPIO36 through a 2:1
// divider (see the vendor utilities.h BATT_PIN). GPIO36 is an ADC1 pin, so it
// stays usable while Wi-Fi is on (ADC2 is claimed by the radio).
static constexpr int kBattPin = 36;

// Average this many samples. A single ADC read carries ±tens-of-mV noise; near
// the brownout gate (kBatteryLowV) that jitter could wrongly trip — or wrongly
// clear — the low-battery path, so we smooth it. 16 reads is well under 1 ms.
static constexpr int kBattSamples = 16;

float batteryVoltage() {
  // analogReadMilliVolts uses the eFuse VRef calibration on this chip, so the
  // result is already in true millivolts (no raw-count -> volts guesswork).
  uint32_t acc = 0;
  for (int i = 0; i < kBattSamples; ++i) acc += analogReadMilliVolts(kBattPin);
  float mv = (float)acc / kBattSamples;
  return (mv * 2.0f) / 1000.0f;  // undo the 2:1 divider
}

int batteryPercent() {
  // Coarse linear map over the usable LiPo span. Real discharge is non-linear,
  // but for a glanceable "roughly how full" indicator this is enough and never
  // reports outside 0..100.
  float v = batteryVoltage();
  float p = (v - 3.30f) / (4.20f - 3.30f) * 100.0f;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (int)(p + 0.5f);
}

}  // namespace app
