// Host unit tests for the pure domain/ layer (PLAN.md §T4).
// Run with: pio test -e native-test
#include <unity.h>

#include "domain/aqi.hpp"
#include "domain/metrics.hpp"
#include "domain/trend.hpp"

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
  m.rco2 = 1800;   // Hazardous -> should win
  m.pm02 = 5;      // Good
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
  return UNITY_END();
}
