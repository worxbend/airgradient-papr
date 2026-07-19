// src/domain/weather.hpp
// Outdoor weather + UV model (Open-Meteo) and geolocation (ip-api). Pure.
#pragma once
#include <cmath>
#include <cstdint>

namespace domain {

struct FcHour {
  char  label[6] = {0};  // "Now", "3PM", "12AM"
  float tempC = NAN;
  int   uv = 0;
};

struct FcDay {
  char  name[5] = {0};   // "Mon"
  float tMax = NAN;
  float tMin = NAN;
  char  cond[12] = {0};  // short condition word
};

struct Weather {
  bool  valid = false;
  float tempC = NAN;
  float apparentC = NAN;
  float humidity = NAN;   // %
  float windKmh = NAN;
  float precip = NAN;     // mm
  float uvIndex = NAN;    // current
  float uvMax = NAN;      // today's max
  float tMax = NAN;
  float tMin = NAN;
  int   isDay = 1;
  char  city[32] = {0};
  char  desc[28] = {0};   // weather description text
  char  sunrise[10] = {0};
  char  sunset[10] = {0};

  // Forecast (from the same wttr.in response).
  FcHour hours[8];
  int    nHours = 0;
  FcDay  days[3];
  int    nDays = 0;
  char   uvWindow[14] = {0};  // "12PM-5PM" high-UV(>=5) window, or ""
  int    uvWindowMax = 0;

  uint32_t ageMs = 0;
};

// WMO weather interpretation code -> short text (Open-Meteo docs).
inline const char* wmoText(int code) {
  switch (code) {
    case 0:  return "Clear sky";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48:  return "Fog";
    case 51: return "Light drizzle";
    case 53: return "Drizzle";
    case 55: return "Dense drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Violent showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm, hail";
    default: return "--";
  }
}

// UV index -> risk band word (WHO).
inline const char* uvWord(float uv) {
  if (std::isnan(uv)) return "--";
  if (uv < 3)  return "Low";
  if (uv < 6)  return "Moderate";
  if (uv < 8)  return "High";
  if (uv < 11) return "Very High";
  return "Extreme";
}

}  // namespace domain
