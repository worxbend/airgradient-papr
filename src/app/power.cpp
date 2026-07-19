// src/app/power.cpp
#include "app/power.hpp"

#include <Arduino.h>

namespace app {

// LilyGo T5-4.7 V1 (classic ESP32): battery sense on GPIO36 through a 2:1
// divider (see the vendor utilities.h BATT_PIN).
static constexpr int kBattPin = 36;

float batteryVoltage() {
  // analogReadMilliVolts uses the eFuse VRef calibration on this chip.
  uint32_t mv = analogReadMilliVolts(kBattPin);
  return (mv * 2.0f) / 1000.0f;
}

int batteryPercent() {
  float v = batteryVoltage();
  float p = (v - 3.30f) / (4.20f - 3.30f) * 100.0f;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (int)(p + 0.5f);
}

}  // namespace app
