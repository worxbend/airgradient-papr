// src/adapters/airgradient_http.hpp
// AirGradient ONE local-server poller: HTTPClient GET + ArduinoJson v7 parse
// into a domain::Measurement (PLAN.md §D4, §T2).
#pragma once
#include <Arduino.h>

#include "domain/measurement.hpp"
#include "ports/metrics_source.hpp"

namespace adapters {

class AirGradientHttp : public ports::MetricsSource {
 public:
  // `url` is the full endpoint, e.g. http://192.168.1.21/measures/current
  explicit AirGradientHttp(const char* url, uint32_t timeoutMs = 4000)
      : url_(url), timeoutMs_(timeoutMs) {}

  bool poll(domain::Measurement& out) override;
  const char* lastError() const override { return err_; }

 private:
  const char* url_;
  uint32_t timeoutMs_;
  char err_[64] = {0};
};

}  // namespace adapters
