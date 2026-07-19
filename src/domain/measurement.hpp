// src/domain/measurement.hpp
// Pure data model for one AirGradient ONE reading. No Arduino deps →
// compiles and is unit-tested on the host (PLAN.md §T4, §D4).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace domain {

inline bool has(float v) {
  return !std::isnan(v);
}

// One decoded /measures/current response. Missing fields stay NaN / empty.
struct Measurement {
  float rco2 = NAN;             // CO2, ppm
  float pm01 = NAN;             // PM1.0, µg/m³
  float pm02 = NAN;             // PM2.5, µg/m³
  float pm10 = NAN;             // PM10, µg/m³
  float pm003Count = NAN;       // particle count 0.3µm
  float atmp = NAN;             // temperature, °C (raw)
  float atmpCompensated = NAN;  // temperature, °C (compensated)
  float rhum = NAN;             // humidity, % (raw)
  float rhumCompensated = NAN;  // humidity, % (compensated)
  float tvocIndex = NAN;        // SGP41 VOC index (0..500, ~100 nominal)
  float tvocRaw = NAN;
  float noxIndex = NAN;  // SGP41 NOx index (~1 nominal)
  float noxRaw = NAN;
  int wifi = 0;  // monitor's own RSSI, dBm
  uint32_t boot = 0;
  uint32_t bootCount = 0;
  char firmware[20] = {0};
  char model[28] = {0};
  char serialno[24] = {0};
  char ledMode[16] = {0};

  bool valid = false;  // true once a poll parsed successfully
  uint32_t ageMs = 0;  // millis() when captured (filled by adapter)

  // Prefer compensated readings when present.
  float tempC() const { return has(atmpCompensated) ? atmpCompensated : atmp; }
  float humidity() const {
    return has(rhumCompensated) ? rhumCompensated : rhum;
  }
};

}  // namespace domain
