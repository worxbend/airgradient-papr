// src/domain/weather_fmt.hpp
// Formatting helpers for the weather page: calendar day names, hour labels and
// the short condition word derived from wttr.in's free-text description.
//
// These live in domain/ rather than next to the HTTP adapter for one reason:
// they are pure string/arithmetic functions with real off-by-one and
// buffer-size edges, and domain/ is the only layer the host `native-test`
// environment can compile (the adapter pulls in <HTTPClient.h>). Keeping them
// here is what makes them testable on a laptop.
//
// Pure: no Arduino, no LVGL, no networking.
#pragma once
#include <cstddef>
#include <cstdio>

namespace domain {

// Day of week for a Gregorian date, 0 = Sunday, via Sakamoto's algorithm.
// Returns -1 when the month is out of range instead of indexing the lookup
// table with m - 1: the caller parses `y`, `m` and `d` out of a string handed
// over by a public server, and a body that matches only part of the pattern
// would otherwise read t[-1].
inline int dowIndex(int y, int m, int d) {
  if (m < 1 || m > 12) return -1;
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

// Name for an index from dowIndex(). A negative index means "unknown date", and
// yields an empty string so the caller can leave its field untouched.
inline const char* dowName(int i) {
  if (i < 0) return "";
  static const char* n[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  return n[i % 7];
}

// 24-hour clock -> "12AM".."11PM".
// Size contract: the longest output is "12AM" (4 characters + NUL), so `n` must
// be at least 5. FcHour::label is char[6], which satisfies that.
inline void hourLabel(int h, char* out, size_t n) {
  int h12 = h % 12;
  if (h12 == 0) h12 = 12;
  snprintf(out, n, "%d%s", h12, h < 12 ? "AM" : "PM");
}

// Case-insensitive substring test over ASCII.
// Written out by hand because neither strcasestr nor strncasecmp is dependable
// across the two toolchains this header has to build under (newlib on the ESP32
// and glibc on the host); ASCII case folding is all the wttr.in descriptions
// need.
inline bool containsCI(const char* hay, const char* needle) {
  if (!hay || !needle) return false;
  for (const char* p = hay; *p; ++p) {
    const char* a = p;
    const char* b = needle;
    while (*b) {
      char ca = *a;
      char cb = *b;
      if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
      if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
      if (ca != cb) break;
      ++a;
      ++b;
    }
    if (!*b) return true;
  }
  return false;
}

// wttr.in's free-text description ("Patchy light drizzle") -> one short
// uppercase word the forecast column has room for ("RAIN"). An empty or absent
// description becomes "--".
// Size contract: the longest output is "OVERCAST" (8 characters + NUL), so `n`
// must be at least 9. FcDay::cond is char[12], which satisfies that.
inline void shortCond(const char* d, char* out, size_t n) {
  auto has = [&](const char* k) { return containsCI(d, k); };
  const char* w = "CLOUDY";
  if (!d || !d[0])
    w = "--";
  else if (has("thunder") || has("storm"))
    w = "STORM";
  else if (has("snow") || has("sleet") || has("blizzard"))
    w = "SNOW";
  else if (has("rain") || has("drizzle") || has("shower"))
    w = "RAIN";
  else if (has("fog") || has("mist"))
    w = "FOG";
  else if (has("sunny") || has("clear"))
    w = "CLEAR";
  else if (has("overcast"))
    w = "OVERCAST";
  else if (has("partly") || has("cloud"))
    w = "CLOUDY";
  // snprintf rather than strlcpy: strlcpy is a BSD extension that host glibc
  // does not provide, and this header must compile under native-test.
  snprintf(out, n, "%s", w);
}

}  // namespace domain
