// src/domain/metrics.hpp
// Central metric catalog: value extraction, banding, labels, hero ranking.
// Pure, host-tested (PLAN.md §3 hero rule, §D4).
#pragma once
#include "domain/aqi.hpp"
#include "domain/measurement.hpp"

namespace domain {

enum class MetricId : uint8_t {
  CO2,
  PM25,
  TVOC,
  NOX,
  PM01,
  PM10,
  PM003,
  TEMP,
  RHUM,
  COUNT
};
static constexpr int kMetricCount = (int)MetricId::COUNT;

struct MetricInfo {
  MetricId id;
  const char* label;
  const char* unit;
  float trendEps;  // |Δ| below this reads as "stable"
  int priority;    // hero tie-break: lower = more important
};

inline const MetricInfo& info(MetricId id) {
  static const MetricInfo tbl[kMetricCount] = {
      // Units use µ/³ (from the custom font_units_18); °C is handled in
      // unitText(). PM strings are "µg/m³".
      {MetricId::CO2, "CO2", "ppm", 5.0f, 0},
      {MetricId::PM25, "PM2.5", "\xC2\xB5g/m\xC2\xB3", 0.5f, 1},
      {MetricId::TVOC, "TVOC", "index", 3.0f, 2},
      {MetricId::NOX, "NOX", "index", 1.0f, 3},
      {MetricId::PM01, "PM1.0", "\xC2\xB5g/m\xC2\xB3", 0.5f, 5},
      {MetricId::PM10, "PM10", "\xC2\xB5g/m\xC2\xB3", 0.5f, 6},
      {MetricId::PM003, "PM0.3", "count", 10.0f, 7},
      {MetricId::TEMP, "TEMP", "C", 0.3f, 4},
      {MetricId::RHUM, "HUM", "%", 1.0f, 4},
  };
  return tbl[(int)id];
}

inline float value(const Measurement& m, MetricId id) {
  switch (id) {
    case MetricId::CO2:
      return m.rco2;
    case MetricId::PM25:
      return m.pm02;
    case MetricId::TVOC:
      return m.tvocIndex;
    case MetricId::NOX:
      return m.noxIndex;
    case MetricId::PM01:
      return m.pm01;
    case MetricId::PM10:
      return m.pm10;
    case MetricId::PM003:
      return m.pm003Count;
    case MetricId::TEMP:
      return m.tempC();
    case MetricId::RHUM:
      return m.humidity();
    default:
      return NAN;
  }
}

inline Band band(const Measurement& m, MetricId id) {
  switch (id) {
    case MetricId::CO2:
      return bandCo2(m.rco2);
    case MetricId::PM25:
      return bandPm25(m.pm02);
    case MetricId::TVOC:
      return bandTvoc(m.tvocIndex);
    case MetricId::NOX:
      return bandNox(m.noxIndex);
    case MetricId::PM01:
      return bandPm25(m.pm01);
    case MetricId::PM10:
      return bandPm25(m.pm10);
    case MetricId::PM003:
      return bandPm25(m.pm003Count > 1000 ? 30 : 5);
    case MetricId::TEMP:
      return bandTemp(m.tempC());
    case MetricId::RHUM:
      return bandRhum(m.humidity());
    default:
      return Band::NoData;
  }
}

// True for temp/RH, which use comfort wording instead of health wording.
inline bool usesComfortWord(MetricId id) {
  return id == MetricId::TEMP || id == MetricId::RHUM;
}

// The worst-status metric right now: highest severity band, then lowest
// priority number. NoData never wins unless everything is NoData.
inline MetricId heroMetric(const Measurement& m) {
  MetricId best = MetricId::CO2;
  int bestSev = -1, bestPrio = 1000;
  for (int i = 0; i < kMetricCount; ++i) {
    MetricId id = (MetricId)i;
    Band b = band(m, id);
    int sev = (b == Band::NoData) ? -1 : (int)b;
    int prio = info(id).priority;
    if (sev > bestSev || (sev == bestSev && prio < bestPrio)) {
      bestSev = sev;
      bestPrio = prio;
      best = id;
    }
  }
  return best;
}

}  // namespace domain
