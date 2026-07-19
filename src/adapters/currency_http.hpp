// src/adapters/currency_http.hpp
// Exchange rates over HTTPS (TLS works now that LVGL lives in PSRAM):
//   - bank.gov.ua (NBU): USD/EUR/CNY in UAH + 30-day USD/UAH history
//   - api.coingecko.com: BTC/ETH in USD
#pragma once
#include <Arduino.h>

#include "domain/currency.hpp"

namespace adapters {

class CurrencyHttp {
 public:
  bool fetch(domain::Currency& out);  // NBU rates + history + crypto
  const char* lastError() const { return err_; }

 private:
  bool tlsGet(const char* url, String& body, uint32_t timeoutMs = 10000);
  char err_[64] = {0};
};

}  // namespace adapters
