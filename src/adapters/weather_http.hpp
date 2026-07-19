// src/adapters/weather_http.hpp
// Outdoor weather + UV via free, key-less public APIs:
//   - ip-api.com  (HTTP)  : IP geolocation -> lat/lon/city
//   - open-meteo  (HTTPS) : current weather + UV + daily
#pragma once
#include <Arduino.h>

#include "domain/weather.hpp"

namespace adapters {

class WeatherHttp {
 public:
  // Resolve lat/lon/city + UTC offset (seconds) from the public IP.
  bool geolocate(double& lat, double& lon, char* city, size_t cityLen,
                 long& utcOffsetSec);

  // Fetch current weather + UV for a location into `out` (wttr.in, plain HTTP).
  bool fetch(double lat, double lon, const char* city, domain::Weather& out);

  const char* lastError() const { return err_; }

 private:
  char err_[64] = {0};
};

}  // namespace adapters
