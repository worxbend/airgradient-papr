// src/domain/trend.hpp
// Ring-buffer history + trend (Δ vs previous reading) over a metric.
// Pure, host-tested (PLAN.md §D4, §T4).
#pragma once
#include <cmath>
#include <cstdint>

namespace domain {

enum class TrendDir : uint8_t { Stable, Up, Down, NoData };

struct Trend {
  TrendDir dir = TrendDir::NoData;
  float delta = NAN;  // latest - previous
};

// Fixed-capacity ring of samples for one metric (sparklines + trend deltas).
template <int N>
class History {
 public:
  void push(float v) {
    buf_[head_] = v;
    head_ = (head_ + 1) % N;
    if (count_ < N) count_++;
  }

  int size() const { return count_; }
  bool empty() const { return count_ == 0; }

  // i = 0 is oldest kept sample, i = size()-1 is newest.
  float at(int i) const {
    int idx = (head_ - count_ + i + N * 2) % N;
    return buf_[idx];
  }
  float latest() const { return count_ ? at(count_ - 1) : NAN; }
  float previous() const { return count_ >= 2 ? at(count_ - 2) : NAN; }
  float oldest() const { return count_ ? at(0) : NAN; }

  float min() const {
    float m = NAN;
    for (int i = 0; i < count_; ++i) {
      float v = at(i);
      if (std::isnan(v)) continue;
      if (std::isnan(m) || v < m) m = v;
    }
    return m;
  }
  float max() const {
    float m = NAN;
    for (int i = 0; i < count_; ++i) {
      float v = at(i);
      if (std::isnan(v)) continue;
      if (std::isnan(m) || v > m) m = v;
    }
    return m;
  }

  // Δ vs the previous reading; "stable" when |Δ| < eps.
  Trend trend(float eps) const {
    Trend t;
    float cur = latest(), prev = previous();
    if (std::isnan(cur) || std::isnan(prev)) {
      t.dir = TrendDir::NoData;
      t.delta = NAN;
      return t;
    }
    t.delta = cur - prev;
    if (std::fabs(t.delta) < eps)
      t.dir = TrendDir::Stable;
    else
      t.dir = (t.delta > 0) ? TrendDir::Up : TrendDir::Down;
    return t;
  }

 private:
  float buf_[N] = {0};
  int head_ = 0;
  int count_ = 0;
};

}  // namespace domain
