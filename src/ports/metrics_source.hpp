// src/ports/metrics_source.hpp
// Port: something that yields a Measurement when polled. Adapters implement it
// (real HTTP on device; a fake generator in the simulator/tests).
#pragma once
#include "domain/measurement.hpp"

namespace ports {

class MetricsSource {
 public:
  virtual ~MetricsSource() = default;

  // Poll once. On success, fills `out` and returns true. On failure returns
  // false and leaves a human-readable reason in lastError().
  virtual bool poll(domain::Measurement& out) = 0;

  virtual const char* lastError() const = 0;
};

}  // namespace ports
