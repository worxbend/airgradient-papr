// Host unit tests for the pure domain/ layer.
// Run with: pio test -e native-test
#include <unity.h>

#include "domain/aqi.hpp"
#include "domain/metrics.hpp"
#include "domain/trend.hpp"
#include "domain/weather_fmt.hpp"

using namespace domain;

void setUp() {}
void tearDown() {}

// ---- AQI (US EPA PM2.5) ----------------------------------------------------
void test_aqi_bounds() {
  TEST_ASSERT_EQUAL_INT(0, epaAqiPm25(0.0f));
  TEST_ASSERT_EQUAL_INT(50, epaAqiPm25(12.0f));
  TEST_ASSERT_EQUAL_INT(100, epaAqiPm25(35.4f));
  TEST_ASSERT_EQUAL_INT(151, epaAqiPm25(55.5f));
  TEST_ASSERT_EQUAL_INT(-1, epaAqiPm25(NAN));
}

void test_aqi_midrange() {
  // 9.0 µg/m³ -> (50/12)*9 = 37.5 -> 38
  TEST_ASSERT_EQUAL_INT(38, epaAqiPm25(9.0f));
}

void test_bands() {
  TEST_ASSERT_TRUE(bandCo2(500) == Band::Good);
  TEST_ASSERT_TRUE(bandCo2(900) == Band::Elevated);
  TEST_ASSERT_TRUE(bandCo2(2000) == Band::Hazardous);
  TEST_ASSERT_TRUE(bandCo2(NAN) == Band::NoData);
  TEST_ASSERT_TRUE(bandTemp(22) == Band::Good);
  TEST_ASSERT_TRUE(bandTemp(35) == Band::Elevated);
  TEST_ASSERT_TRUE(bandRhum(50) == Band::Good);
}

// ---- Trend -----------------------------------------------------------------
void test_trend_up_down_stable() {
  History<8> h;
  h.push(10.0f);
  h.push(12.0f);
  Trend up = h.trend(0.5f);
  TEST_ASSERT_TRUE(up.dir == TrendDir::Up);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, up.delta);

  History<8> h2;
  h2.push(10.0f);
  h2.push(10.1f);
  TEST_ASSERT_TRUE(h2.trend(0.5f).dir == TrendDir::Stable);

  History<8> h3;
  h3.push(10.0f);
  h3.push(7.0f);
  TEST_ASSERT_TRUE(h3.trend(0.5f).dir == TrendDir::Down);
}

void test_trend_nodata() {
  History<8> h;
  TEST_ASSERT_TRUE(h.trend(0.5f).dir == TrendDir::NoData);
  h.push(5.0f);
  TEST_ASSERT_TRUE(h.trend(0.5f).dir == TrendDir::NoData);  // needs 2 samples
}

void test_ring_wraps_and_minmax() {
  History<4> h;
  for (int i = 1; i <= 6; ++i) h.push((float)i);  // keeps 3,4,5,6
  TEST_ASSERT_EQUAL_INT(4, h.size());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, h.latest());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, h.oldest());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, h.min());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, h.max());
}

// ---- Hero selection --------------------------------------------------------
void test_hero_picks_worst() {
  Measurement m;
  m.valid = true;
  m.rco2 = 1800;  // Hazardous -> should win
  m.pm02 = 5;     // Good
  m.tvocIndex = 100;
  m.noxIndex = 1;
  m.atmp = 22;
  m.rhum = 50;
  TEST_ASSERT_TRUE(heroMetric(m) == MetricId::CO2);
}

void test_hero_priority_tiebreak() {
  Measurement m;
  m.valid = true;
  m.rco2 = 500;       // Good
  m.pm02 = 5;         // Good
  m.tvocIndex = 300;  // Elevated (prio 2)
  m.noxIndex = 100;   // Elevated (prio 3)
  m.atmp = 22;
  m.rhum = 50;
  // Both Elevated; TVOC has lower priority number -> hero.
  TEST_ASSERT_TRUE(heroMetric(m) == MetricId::TVOC);
}

// PM10 uses its own (looser) EPA breakpoints, not PM2.5's: 60 µg/m³ of PM10
// is an EPA-Moderate day, while 60 of PM2.5 would be Unhealthy.
void test_pm10_uses_own_breakpoints() {
  TEST_ASSERT_TRUE(bandPm10(NAN) == Band::NoData);
  TEST_ASSERT_TRUE(bandPm10(54.0f) == Band::Good);
  TEST_ASSERT_TRUE(bandPm10(60.0f) == Band::Moderate);
  TEST_ASSERT_TRUE(bandPm25(60.0f) == Band::Unhealthy);  // the old, wrong band
  TEST_ASSERT_TRUE(bandPm10(200.0f) == Band::Elevated);
  TEST_ASSERT_TRUE(bandPm10(400.0f) == Band::Hazardous);
}

// The AirGradient's own display shows EPA-compensated PM2.5; papr must agree.
// Temperature and humidity have the same "prefer compensated, fall back to raw"
// shape and the same way of silently regressing, so they are pinned here too.
void test_pm25_prefers_compensated() {
  Measurement m;
  m.pm02 = 0.67f;
  TEST_ASSERT_EQUAL_FLOAT(0.67f, m.pm25());  // raw only -> raw
  m.pm02Compensated = 7.37f;
  TEST_ASSERT_EQUAL_FLOAT(7.37f, m.pm25());  // compensated wins when present

  m.atmp = 21.5f;
  TEST_ASSERT_EQUAL_FLOAT(21.5f, m.tempC());
  m.atmpCompensated = 20.25f;
  TEST_ASSERT_EQUAL_FLOAT(20.25f, m.tempC());

  m.rhum = 55.0f;
  TEST_ASSERT_EQUAL_FLOAT(55.0f, m.humidity());
  m.rhumCompensated = 48.5f;
  TEST_ASSERT_EQUAL_FLOAT(48.5f, m.humidity());
}

// The UI banner reads heroMetric() before the first successful poll, when every
// field is still NaN. This pins what it answers then. The result is PM0.3
// rather than CO2, and that is worth spelling out: metrics.hpp seeds the search
// with severity -1 so that a NoData metric never wins, but PM0.3 is banded via
// `bandPm25(pm003Count > 1000 ? 30 : 5)`, and a NaN count compares false, so an
// absent particle count still bands as Good and outranks the eight NoData
// metrics. Characterization, not endorsement: if that mapping is ever changed
// to propagate NoData, this expectation should become MetricId::CO2.
void test_hero_all_nodata_falls_back_to_pm003() {
  Measurement m;  // default-constructed: every reading NaN, valid == false
  TEST_ASSERT_TRUE(band(m, MetricId::CO2) == Band::NoData);
  TEST_ASSERT_TRUE(band(m, MetricId::PM003) == Band::Good);
  TEST_ASSERT_TRUE(heroMetric(m) == MetricId::PM003);
}

// TVOC and NOx stop at Unhealthy on purpose — the SGP41 indices are relative,
// not a dose, so there is no defensible "Hazardous" line to draw. Every other
// band function does reach Hazardous, which makes this asymmetry easy to
// "fix" by accident.
void test_tvoc_and_nox_never_reach_hazardous() {
  TEST_ASSERT_TRUE(bandTvoc(1000.0f) == Band::Unhealthy);
  TEST_ASSERT_TRUE(bandNox(1000.0f) == Band::Unhealthy);
  TEST_ASSERT_TRUE(bandCo2(5000.0f) == Band::Hazardous);  // contrast
}

// Above the top EPA breakpoint (500.4 µg/m³) the AQI is clamped to 500 rather
// than extrapolated, so an absurd sensor reading still renders a sane number.
void test_aqi_clamps_above_top_breakpoint() {
  TEST_ASSERT_EQUAL_INT(500, epaAqiPm25(900.0f));
  TEST_ASSERT_TRUE(bandPm25(900.0f) == Band::Hazardous);
}

// previous() is the only History accessor that indexes count_ - 2, so it is the
// one that breaks if the wrapped index arithmetic in at() regresses.
void test_ring_previous_after_wrap() {
  History<4> h;
  for (int i = 1; i <= 6; ++i) h.push((float)i);  // keeps 3,4,5,6
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, h.latest());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, h.previous());
}

// ---- Weather formatting ----------------------------------------------------
// 2024-08-21 was a Wednesday (index 3).
void test_dow_index_known_date() {
  TEST_ASSERT_EQUAL_INT(3, dowIndex(2024, 8, 21));
  TEST_ASSERT_EQUAL_STRING("Wed", dowName(dowIndex(2024, 8, 21)));
}

// A partially-parsed date from the weather server leaves the month at 0 (or
// anything else out of range). dowIndex must reject it instead of indexing its
// 12-entry table at t[-1].
void test_dow_index_rejects_bad_month() {
  TEST_ASSERT_EQUAL_INT(-1, dowIndex(2024, 0, 21));
  TEST_ASSERT_EQUAL_INT(-1, dowIndex(2024, 13, 21));
  TEST_ASSERT_EQUAL_STRING("", dowName(-1));
}

void test_hour_label() {
  char buf[6];  // same size as FcHour::label
  hourLabel(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("12AM", buf);
  hourLabel(12, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("12PM", buf);
  hourLabel(15, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("3PM", buf);
}

void test_short_cond() {
  char buf[12];  // same size as FcDay::cond
  shortCond("Patchy light drizzle", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("RAIN", buf);
  shortCond("", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("--", buf);
  // "OVERCAST" is the longest word this can produce (8 chars + NUL): it must
  // survive intact in the buffer the forecast column actually uses.
  shortCond("Overcast", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("OVERCAST", buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_aqi_bounds);
  RUN_TEST(test_aqi_midrange);
  RUN_TEST(test_bands);
  RUN_TEST(test_trend_up_down_stable);
  RUN_TEST(test_trend_nodata);
  RUN_TEST(test_ring_wraps_and_minmax);
  RUN_TEST(test_hero_picks_worst);
  RUN_TEST(test_hero_priority_tiebreak);
  RUN_TEST(test_pm25_prefers_compensated);
  RUN_TEST(test_pm10_uses_own_breakpoints);
  RUN_TEST(test_hero_all_nodata_falls_back_to_pm003);
  RUN_TEST(test_tvoc_and_nox_never_reach_hazardous);
  RUN_TEST(test_aqi_clamps_above_top_breakpoint);
  RUN_TEST(test_ring_previous_after_wrap);
  RUN_TEST(test_dow_index_known_date);
  RUN_TEST(test_dow_index_rejects_bad_month);
  RUN_TEST(test_hour_label);
  RUN_TEST(test_short_cond);
  return UNITY_END();
}
