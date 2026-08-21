// src/adapters/airgradient_http.hpp
// AirGradient ONE local-server poller: HTTPClient GET + ArduinoJson v7 parse
// into a domain::Measurement.
#pragma once
#include <Arduino.h>

#include "domain/measurement.hpp"

namespace adapters {

class AirGradientHttp {
 public:
  // `url` is the full endpoint, e.g. http://192.168.1.21/measures/current
  explicit AirGradientHttp(const char* url, uint32_t timeoutMs = 4000)
      : url_(url), timeoutMs_(timeoutMs) {}

  // Poll once. On success, fills `out` and returns true. On failure returns
  // false and leaves a human-readable reason in lastError().
  bool poll(domain::Measurement& out);
  const char* lastError() const { return err_; }

 private:
  const char* url_;
  uint32_t timeoutMs_;
  char err_[64] = {0};
};

}  // namespace adapters
