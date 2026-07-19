// src/domain/currency.hpp
// Exchange rates + 30-day USD/UAH history. Pure data.
#pragma once
#include <cmath>
#include <cstdint>

namespace domain {

constexpr int kHistMax = 32;

struct Currency {
  bool valid = false;
  float usdUah = NAN;   // 1 USD -> UAH (NBU)
  float eurUah = NAN;   // 1 EUR -> UAH (NBU)
  float cnyUsd = NAN;   // 1 CNY -> USD (derived from NBU)
  float btcUsd = NAN;   // 1 BTC -> USD (CoinGecko)
  float ethUsd = NAN;   // 1 ETH -> USD (CoinGecko)
  char date[12] = {0};  // NBU exchange date

  float hist[kHistMax] = {};  // USD/UAH history, oldest -> newest
  int histCount = 0;

  uint32_t ageMs = 0;
};

}  // namespace domain
