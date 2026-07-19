// src/domain/aqi.hpp
// US EPA AQI (PM2.5) + status banding for CO2/TVOC/NOx/temp/RH.
// Pure, host-tested (PLAN.md §D4, §T4).
#pragma once
#include <cmath>
#include <cstdint>

namespace domain {

// Severity-ordered status band. Higher value = worse. NoData ranks lowest
// for hero selection (handled by the caller).
enum class Band : uint8_t {
  Good = 0,
  Moderate = 1,
  Elevated = 2,
  Unhealthy = 3,
  Hazardous = 4,
  NoData = 5,
};

inline const char* bandWord(Band b) {
  switch (b) {
    case Band::Good:       return "Good";
    case Band::Moderate:   return "Moderate";
    case Band::Elevated:   return "Elevated";
    case Band::Unhealthy:  return "Unhealthy";
    case Band::Hazardous:  return "Hazardous";
    default:               return "--";
  }
}

// For temp/RH the app uses comfort language instead of health language.
inline const char* comfortWord(Band b) {
  switch (b) {
    case Band::Good:      return "Comfort";
    case Band::Moderate:  return "Fair";
    case Band::Elevated:  return "Poor";
    case Band::Unhealthy: return "Extreme";
    case Band::Hazardous: return "Extreme";
    default:              return "--";
  }
}

// ---- US EPA AQI from PM2.5 (µg/m³). Returns -1 for no data. ----------------
inline int epaAqiPm25(float pm25) {
  if (std::isnan(pm25) || pm25 < 0.0f) return -1;
  // Concentration is truncated to 0.1 µg/m³ per EPA method.
  const float c = std::floor(pm25 * 10.0f) / 10.0f;
  struct BP { float clo, chi; int ilo, ihi; };
  static const BP table[] = {
      {0.0f,   12.0f,   0,   50},
      {12.1f,  35.4f,   51,  100},
      {35.5f,  55.4f,   101, 150},
      {55.5f,  150.4f,  151, 200},
      {150.5f, 250.4f,  201, 300},
      {250.5f, 350.4f,  301, 400},
      {350.5f, 500.4f,  401, 500},
  };
  for (const auto& bp : table) {
    if (c >= bp.clo && c <= bp.chi) {
      return (int)std::lround((float)(bp.ihi - bp.ilo) / (bp.chi - bp.clo) *
                                  (c - bp.clo) +
                              (float)bp.ilo);
    }
  }
  return 500;  // above the top breakpoint
}

inline Band bandFromAqi(int aqi) {
  if (aqi < 0)   return Band::NoData;
  if (aqi <= 50) return Band::Good;
  if (aqi <= 100) return Band::Moderate;
  if (aqi <= 150) return Band::Elevated;
  if (aqi <= 200) return Band::Unhealthy;
  return Band::Hazardous;
}

inline Band bandPm25(float pm25) { return bandFromAqi(epaAqiPm25(pm25)); }

inline Band bandCo2(float ppm) {
  if (std::isnan(ppm))  return Band::NoData;
  if (ppm <= 600)  return Band::Good;
  if (ppm <= 800)  return Band::Moderate;
  if (ppm <= 1000) return Band::Elevated;
  if (ppm <= 1500) return Band::Unhealthy;
  return Band::Hazardous;
}

// SGP41 VOC index: 100 nominal, higher = more VOCs.
inline Band bandTvoc(float idx) {
  if (std::isnan(idx)) return Band::NoData;
  if (idx <= 150) return Band::Good;
  if (idx <= 250) return Band::Moderate;
  if (idx <= 400) return Band::Elevated;
  return Band::Unhealthy;
}

// SGP41 NOx index: 1 nominal, higher = more NOx.
inline Band bandNox(float idx) {
  if (std::isnan(idx)) return Band::NoData;
  if (idx <= 1)   return Band::Good;
  if (idx <= 20)  return Band::Moderate;
  if (idx <= 150) return Band::Elevated;
  return Band::Unhealthy;
}

inline Band bandTemp(float c) {
  if (std::isnan(c)) return Band::NoData;
  if (c >= 18.0f && c <= 25.0f) return Band::Good;
  if (c >= 15.0f && c <= 28.0f) return Band::Moderate;
  return Band::Elevated;
}

inline Band bandRhum(float pct) {
  if (std::isnan(pct)) return Band::NoData;
  if (pct >= 40.0f && pct <= 60.0f) return Band::Good;
  if (pct >= 30.0f && pct <= 70.0f) return Band::Moderate;
  return Band::Elevated;
}

}  // namespace domain
